// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "PeerDoor.h"
#include "Peer.h"
#include "main/Application.h"
#include "main/Config.h"
#include "overlay/OverlayManager.h"
#include "overlay/TCPPeer.h"
#include "util/Logging.h"
#include <memory>

namespace stellar {

  using namespace std;

  PeerDoor::PeerDoor(Application &app) : mApp(app) {
  }

  void
  PeerDoor::start() {
    if (!mApp.getConfig().RUN_STANDALONE) {
      // tcp::endpoint endpoint(tcp::v4(), mApp.getConfig().PEER_PORT);
      // CLOG(INFO, "Overlay") << "Binding to endpoint " << endpoint;
//        CLOG(INFO, "Overlay") << "No endpoint, lol, name: " << mApp.getName().toString();

      // mAcceptor.open(endpoint.protocol());
      // mAcceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
      // mAcceptor.bind(endpoint);
      // mAcceptor.listen();
      acceptNextPeer();
    }
  }

  void
  PeerDoor::close() {
    // if (mAcceptor.is_open())
    // {
    //     asio::error_code ec;
    //     // ignore errors when closing
    //     mAcceptor.close(ec);
    // }
  }

  void PeerDoor::MH::handle(const string &m) {
//    std::cout << "Received message!" << std::endl;
//    std::cout << "Message:" << m << std::endl;
//    std::cout << "getMessageSender:" << TCPPeer::getMessageSender(m).toString() << std::endl;
//    std::cout << "getMessageReceiver:" << TCPPeer::getMessageReceiver(m).toString() << std::endl;
//    std::cout << "MyNAME:" << myName.toString() << std::endl;
    if (TCPPeer::getMessageReceiver(m) != myName) {
    //  std::cout << "Message's not for me" << std::endl;
      return;
    } else {
   //   std::cout << "My message" << std::endl;
    }

    if (m.find("INIT") < 0) {
   //   std::cout << "Not init message" << std::endl;
      return;
    }

    auto peerName = TCPPeer::getMessageSender(m);
    PD->mApp.postOnMainThread([this, peerName]() {
      PD->handleKnock(MB, myName, peerName);
    }, "PeerDoor: MH::handle");

  }

  void
  PeerDoor::acceptNextPeer() {
    if (mApp.getOverlayManager().isShuttingDown()) {
      return;
    }
    auto name = my::PeerName(mApp.getConfig().PEER_NAME);
//    std::vector<std::string> names = {peerName.toString(), myName.toString()};
//    std::sort(names.begin(), names.end());
//    auto roomId = names[0] + names[1];

    callbacks.emplace_back(std::make_shared<MH>(mApp.getOverlayManager().getMB(), name, this));

    //mApp.getOverlayManager().getMB()->joinRoom(DEFAULT_ROOM_ID);
    mApp.getOverlayManager().getMB()->addCallbackToRoom(DEFAULT_ROOM_ID, callbacks.back());

    CLOG(INFO, "Overlay") << "Listening room, my name: " << name.toString();
    CLOG(DEBUG, "Overlay") << "PeerDoor acceptNextPeer()";
  }

  void
  PeerDoor::handleKnock(std::shared_ptr<messageBroker> MB, my::PeerName const &myName, my::PeerName const &peerName) {
    CLOG(DEBUG, "Overlay") << "PeerDoor handleKnock() @"
                           << myName.toString();
    Peer::pointer peer = TCPPeer::accept(mApp, MB, myName, peerName);
    if (peer) {
      mApp.getOverlayManager().addInboundConnection(peer);
    }
  }

  PeerDoor::~PeerDoor() {
    for (const auto &x: callbacks) {
      mApp.getOverlayManager().getMB()->removeCallbackFromRoom(DEFAULT_ROOM_ID, x);
    }
  }
}