// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/TCPPeer.h"
#include "database/Database.h"
#include "main/Application.h"
#include "main/Config.h"
#include "main/ErrorMessages.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "overlay/LoadManager.h"
#include "overlay/OverlayManager.h"
#include "overlay/OverlayMetrics.h"
#include "overlay/PeerManager.h"
#include "overlay/StellarXDR.h"
#include "util/GlobalChecks.h"
#include "util/Logging.h"
#include "xdrpp/marshal.h"

using namespace soci;

namespace stellar {

  using namespace std;

///////////////////////////////////////////////////////////////////////
// TCPPeer
///////////////////////////////////////////////////////////////////////

  TCPPeer::TCPPeer(Application &app, Peer::PeerRole role, std::shared_ptr<messageBroker> MB)
      : Peer(app, role), mMB(move(MB)) {
  }

  TCPPeer::pointer
  TCPPeer::initiate(Application &app, my::PeerName const &peerName) {
    CLOG(DEBUG, "Overlay") << "TCPPeer:initiate"
                           << " to " << peerName.toString();
    assertThreadIsMain();

    auto myName = my::PeerName(app.getConfig().PEER_NAME);

    auto result = make_shared<TCPPeer>(app, WE_CALLED_REMOTE, app.getOverlayManager().getMB());
    result->mPeerName = peerName;
    result->mMyName = myName;
    std::vector<std::string> names = {peerName.toString(), myName.toString()};
    std::sort(names.begin(), names.end());
    auto roomId = names[0] + names[1];
    //result->mMB->joinRoom(DEFAULT_ROOM_ID);
    result->startRead();
    const string initMessage = myName.toString() + peerName.toString() + "INIT";
    //result->mMB->broadcast2Room(DEFAULT_ROOM_ID, initMessage);
    result->mMB->send2client(peerName.toString() + DEFAULT_ROOM_ID, DEFAULT_ROOM_ID, initMessage);
//    for (const auto x: result->mMB->getNodeIds(DEFAULT_ROOM_ID)) {
//      std::cout << x << std::endl;
//    }
//    result->connectHandler();

    return result;
  }

  TCPPeer::pointer
  TCPPeer::accept(Application &app, std::shared_ptr<messageBroker> MB, my::PeerName myName, my::PeerName peerName) {
    assertThreadIsMain();
    shared_ptr<TCPPeer> result;
    CLOG(DEBUG, "Overlay") << "TCPPeer:accept"
                           << "@" << myName.toString();
    result = make_shared<TCPPeer>(app, REMOTE_CALLED_US, move(MB));
    result->mPeerName = move(peerName);
    result->mMyName = move(myName);
    result->startRead();
    StellarMessage m(MessageType::ACCEPT);
    result->Peer::sendMessage(m);

    return result;
  }

  TCPPeer::~TCPPeer() {
//    if (t && t->joinable()) {
//      t->join();
//    }
    // assertThreadIsMain();
    if (callback && mApp.getOverlayManager().getMB()->removeCallbackFromRoom(DEFAULT_ROOM_ID, callback)) {
    } else {
      std::cout << "Could not erase" << std::endl;
    }
    mIdleTimer.cancel();
  }

  void
  TCPPeer::sendMessage(xdr::msg_ptr &&xdrBytes) {
    if (mState == CLOSING) {
      CLOG(ERROR, "Overlay")
          << "Trying to send message to " << toString() << " after drop";
      CLOG(ERROR, "Overlay") << REPORT_INTERNAL_BUG;
      return;
    }

    if (Logging::logTrace("Overlay"))
      CLOG(TRACE, "Overlay") << "TCPPeer:sendMessage to " << toString();
    // assertThreadIsMain();

    // places the buffer to write into the write queue
    auto buf = std::make_shared<xdr::msg_ptr>(std::move(xdrBytes));

    auto self = static_pointer_cast<TCPPeer>(shared_from_this());

    self->mWriteQueue.emplace(buf);

    if (!self->mWriting) {
      self->mWriting = true;
      // kick off the async write chain if we're the first one
      self->messageSender();
    }
  }

  void TCPPeer::MH::handle(const std::string &m) {
//    std::cout << "Received message!" << std::endl;
//    std::cout << "Message:" << m << std::endl;
//    std::cout << "getMessageSender:" << getMessageSender(m).toString() << std::endl;
//    std::cout << "getMessageReceiver:" << getMessageReceiver(m).toString() << std::endl;
//    std::cout << "MyNAME:" << self->getMyName().toString() << std::endl;
    if (getMessageSender(m) != self->getName()) {
      //std::cout << "Message's not for me" << std::endl;
      return;
    }
    if (getMessageReceiver(m) != self->getMyName()) {
      // std::cout << "Message's not for this peer handler" << std::endl;
      return;
    }

    //std::cout << "My message" << std::endl;

    self->mIncomingMessage = getMessage(m);
    if (self->mIncomingMessage == "INIT") {
      std::cout << "Init message but already inited, IGNORE" << std::endl;
      return;
    }

    if (Logging::logTrace("Overlay")) {
      CLOG(TRACE, "Overlay")
          << "TCPPeer::startRead calledback " << std::endl
          << " length:" << self->mIncomingMessage.length();
    }
    if (self) {
      self->getApp().postOnMainThread([this]() {
        self->readMessageHandler();
      }, "TCPPeer: MH::handle");
    } else {
      std::cout << "!self" << std::endl;
    }
//self->readMessageHandler();
  }

//todo
  void
  TCPPeer::shutdown() {
    if (mShutdownScheduled) {
      // should not happen, leave here for debugging purposes
      CLOG(ERROR, "Overlay") << "Double schedule of shutdown " << toString();
      CLOG(ERROR, "Overlay") << REPORT_INTERNAL_BUG;
      return;
    }

    mIdleTimer.cancel();
    mShutdownScheduled = true;
    auto self = static_pointer_cast<TCPPeer>(shared_from_this());
    if (callback && mApp.getOverlayManager().getMB()->removeCallbackFromRoom(DEFAULT_ROOM_ID, callback)) {
    } else {
      std::cout << "Could not erase" << std::endl;
    }
    // To shutdown, we first queue up our desire to shutdown in the strand,
    // behind any pending read/write calls. We'll let them issue first.
    self->getApp().postOnMainThread(
        [self]() {
          // Gracefully shut down connection: this pushes a FIN packet into
          // TCP which, if we wanted to be really polite about, we would wait
          // for an ACK from by doing repeated reads until we get a 0-read.
          //
          // But since we _might_ be dropping a hostile or unresponsive
          // connection, we're going to just post a close() immediately after,
          // and hope the kernel does something useful as far as putting any
          // queued last-gasp ERROR_MSG packet on the wire.
          //
          // All of this is voluntary. We can also just close(2) here and be
          // done with it, but we want to give some chance of telling peers
          // why we're disconnecting them.
//            asio::error_code ec;
//            self->mSocket->next_layer().shutdown(
//                asio::ip::tcp::socket::shutdown_both, ec);
//            if (ec)
//            {
//                CLOG(DEBUG, "Overlay")
//                    << "TCPPeer::drop shutdown socket failed: " << ec.message();
//            }
          self->getApp().postOnMainThread(
              [self]() {
                // Close fd associated with socket. Socket is already shut
                // down, but depending on platform (and apparently whether
                // there was unread data when we issued shutdown()) this
                // call might push RST onto the wire, or some other action;
                // in any case it has to be done to free the OS resources.
                //
                // It will also, at this point, cancel any pending asio
                // read/write handlers, i.e. fire them with an error code
                // indicating cancellation.
//                    asio::error_code ec2;
//                    self->mSocket->close(ec2);
//                    if (ec2)
//                    {
//                        CLOG(DEBUG, "Overlay")
//                            << "TCPPeer::drop close socket failed: "
//                            << ec2.message();
//                    }
              },
              "TCPPeer: close");
        },
        "TCPPeer: shutdown");
  }

  void
  TCPPeer::messageSender() {
    assertThreadIsMain();

    auto self = static_pointer_cast<TCPPeer>(shared_from_this());

    // if nothing to do, flush and return
    if (mWriteQueue.empty()) {
      mLastEmpty = mApp.getClock().now();
      self->writeHandler(0);
      if (!self->mWriteQueue.empty()) {
        self->messageSender();
      } else {
        self->mWriting = false;
        // there is nothing to send and delayed shutdown was
        // requested - time to perform it
        if (self->mDelayedShutdown) {
          self->shutdown();
        }
      }
      return;
    }
    std::vector<std::string> names = {self->mPeerName.toString(), self->mMyName.toString()};
    std::sort(names.begin(), names.end());
    auto roomId = names[0] + names[1];
    while (not mWriteQueue.empty()) {
      auto buf = mWriteQueue.front();
      auto msg = mMyName.toString() + mPeerName.toString() +
                 std::string((buf->get())->data(), (buf->get())->end());
      mMB->send2client(mPeerName.toString() + DEFAULT_ROOM_ID, DEFAULT_ROOM_ID, msg);
//      mMB->broadcast2Room(DEFAULT_ROOM_ID, mMyName.toString() + mPeerName.toString() +
//                                           std::string((buf->get())->data(), (buf->get())->end()));
      self->writeHandler((*buf.get())->size());
      self->mWriteQueue.pop();
      self->messageSender();
    }
  }

  void
  TCPPeer::writeHandler(std::size_t bytes_transferred) {
    // assertThreadIsMain();
    mLastWrite = mApp.getClock().now();
    if (bytes_transferred != 0) {
      LoadManager::PeerContext loadCtx(mApp, mPeerID);
      getOverlayMetrics().mMessageWrite.Mark();
      getOverlayMetrics().mByteWrite.Mark(bytes_transferred);
    }
  }

  void
  TCPPeer::startRead() {
    // assertThreadIsMain();
    if (shouldAbort()) {
      return;
    }

    auto self = static_pointer_cast<TCPPeer>(shared_from_this());

    if (Logging::logTrace("Overlay"))
      CLOG(TRACE, "Overlay") << "TCPPeer::startRead " << self->mMyName.toString() << " to " << self->toString();
    std::cout << "TCPPeer::startRead " << self->mMyName.toString() << " to " << self->toString() << std::endl;
    callback = std::make_shared<MH>(self);
    std::vector<std::string> names = {self->mPeerName.toString(), self->mMyName.toString()};
    std::sort(names.begin(), names.end());
    auto roomId = names[0] + names[1];
    mMB->addCallbackToRoom(DEFAULT_ROOM_ID, callback);
  }

//  void
//  TCPPeer::connected() {
//    startRead();
//  }

  void
  TCPPeer::readMessageHandler() {
    receivedBytes(mIncomingMessage.length(), true);
    recvMessage();
  }

  void
  TCPPeer::recvMessage() {
    assertThreadIsMain();
    try {
      xdr::xdr_get g(mIncomingMessage.data(),
                     mIncomingMessage.data() + mIncomingMessage.size());
      AuthenticatedMessage am;
      xdr::xdr_argpack_archive(g, am);
      Peer::recvMessage(am);
    }
    catch (xdr::xdr_runtime_error &e) {
      CLOG(ERROR, "Overlay") << "recvMessage got a corrupt xdr: " << e.what();
      sendErrorAndDrop(ERR_DATA, "received corrupt XDR",
                       Peer::DropMode::IGNORE_WRITE_QUEUE);
    }
  }

  void
  TCPPeer::drop(std::string const &reason, DropDirection dropDirection,
                DropMode dropMode) {
    // assertThreadIsMain();
    if (shouldAbort()) {
      return;
    }

    if (mState != GOT_AUTH) {
      CLOG(DEBUG, "Overlay") << "TCPPeer::drop " << toString() << " in state "
                             << mState << " we called:" << mRole;
    } else if (dropDirection == Peer::DropDirection::WE_DROPPED_REMOTE) {
      CLOG(INFO, "Overlay")
          << "Dropping peer " << toString() << "; reason: " << reason;
    } else {
      CLOG(INFO, "Overlay")
          << "Peer " << toString() << " dropped us; reason: " << reason;
    }

    mState = CLOSING;

    auto self = static_pointer_cast<TCPPeer>(shared_from_this());
    getApp().getOverlayManager().removePeer(this);

    // if write queue is not empty, messageSender will take care of shutdown
    if ((dropMode == Peer::DropMode::IGNORE_WRITE_QUEUE) || !mWriting) {
      self->shutdown();
    } else {
      self->mDelayedShutdown = true;
    }
  }
}
