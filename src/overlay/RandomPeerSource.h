#pragma once

// Copyright 2019 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/PeerManager.h"

#include <functional>
#include <vector>

namespace stellar
{

enum class PeerType;
class PeerManager;
struct PeerQuery;

class RandomPeerSource
{
  public:
    static PeerQuery maxFailures(int maxFailures, bool outbound);
    static PeerQuery nextAttemptCutoff(PeerType peerType);

    explicit RandomPeerSource(PeerManager& peerManager, PeerQuery peerQuery);

    std::vector<my::PeerName>
    getRandomPeers(int size, std::function<bool(my::PeerName const&)> pred);

  private:
    PeerManager& mPeerManager;
    PeerQuery const mPeerQuery;
    std::vector<my::PeerName> mPeerCache;
};
}
