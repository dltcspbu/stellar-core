#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "TCPPeer.h"
#include <memory>

/*
listens for peer connections.
When found passes them to the OverlayManagerImpl
*/

namespace stellar {
  class Application;

  class PeerDoorStub;

  class PeerDoor {
  protected:
    Application &mApp;
  public:
    class MH : public messageHandler {
      std::shared_ptr<messageBroker> MB;
      my::PeerName myName;
      PeerDoor *PD;
    public:
      MH(std::shared_ptr<messageBroker> ptr, my::PeerName const &name, PeerDoor *PDPtr) : MB(move(ptr)), myName(name),
                                                                                          PD(PDPtr) {}

      ~MH() {
        std::cout << "CALLED" << std::endl;
      }

      void handle(const string &m) override;
    };

  protected:
    std::vector<::shared_ptr<MH>> callbacks;

    virtual void acceptNextPeer();

    virtual void
    handleKnock(std::shared_ptr<messageBroker> MB, my::PeerName const &myName, my::PeerName const &peerName);

    friend PeerDoorStub;

  public:
    typedef std::shared_ptr<PeerDoor> pointer;

    explicit PeerDoor(Application &);

    void start();

    void close();

    ~PeerDoor();
  };
}
