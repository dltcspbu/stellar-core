#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "util/Logging.h"
#include "overlay/Peer.h"
#include "util/Timer.h"
#include "messageBroker.hpp"
#include <queue>
#include <src/my_classes/Name.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

namespace medida {
  class Meter;
}

namespace stellar {

  static auto const MAX_UNAUTH_MESSAGE_SIZE = 0x1000;
  static auto const MAX_MESSAGE_SIZE = 0x1000000;

// Peer that communicates via a TCP socket.
  class TCPPeer : public Peer {
    std::shared_ptr<messageBroker> mMB;
  public:
    class MH : public messageHandler {
      std::shared_ptr<TCPPeer> self;
    public:
      MH(std::shared_ptr<TCPPeer> ptr) : self(move(ptr)) {}

      void handle(const std::string &m) override;
    };

  private:
    std::string mIncomingMessage;
    std::shared_ptr<MH> callback;
    std::queue<std::shared_ptr<xdr::msg_ptr>> mWriteQueue;
    bool mWriting{false};
    bool mDelayedShutdown{false};
    bool mShutdownScheduled{false};

    void recvMessage();

    void sendMessage(xdr::msg_ptr &&xdrBytes) override;

    void messageSender();

//    void connected() override;

    void startRead();

    void writeHandler(std::size_t bytes_transferred) override;

    void readMessageHandler() override;

    void shutdown();

  public:
    typedef std::shared_ptr<TCPPeer> pointer;

    TCPPeer(Application &app, Peer::PeerRole role, std::shared_ptr<messageBroker> MB); // hollow
    // constuctor; use
    // `initiate` or
    // `accept` instead

    static pointer initiate(Application &app, my::PeerName const &peerName);

    static pointer
    accept(Application &app, std::shared_ptr<messageBroker> MB, my::PeerName myName, my::PeerName peerName);

    ~TCPPeer() override;

    void drop(std::string const &reason, DropDirection dropDirection,
              DropMode dropMode) override;

    static my::PeerName getMessageSender(string const &messageJSON) {
      rapidjson::Document d;
      d.Parse(messageJSON.data(), messageJSON.size());
      assert(d.IsObject());
      assert(d.HasMember("data"));
      assert(d["data"].IsString());
      string message = d["data"].GetString();
      return my::PeerName(message.substr(0, my::DEFAULT_NAME_LENGTH));
    }

    static my::PeerName getMessageReceiver(string const &messageJSON) {
      rapidjson::Document d;
      d.Parse(messageJSON.data(), messageJSON.size());
      assert(d.IsObject());
      assert(d.HasMember("data"));
      assert(d["data"].IsString());
      string message = d["data"].GetString();
      return my::PeerName(message.substr(my::DEFAULT_NAME_LENGTH, my::DEFAULT_NAME_LENGTH));
    }

    static string getMessage(string const &messageJSON) {
      rapidjson::Document d;
      d.Parse(messageJSON.data(), messageJSON.size());
      assert(d.IsObject());
      assert(d.HasMember("data"));
      assert(d["data"].IsString());
      string message = d["data"].GetString();
      int start = my::DEFAULT_NAME_LENGTH * 2;
      return message.substr(start, message.length());
    }
  };
}
