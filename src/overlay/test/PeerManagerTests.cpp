// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Database.h"
#include "lib/catch.hpp"
#include "main/Application.h"
#include "main/Config.h"
#include "overlay/OverlayManager.h"
#include "overlay/PeerManager.h"
#include "overlay/RandomPeerSource.h"
#include "overlay/StellarXDR.h"
#include "test/TestUtils.h"
#include "test/test.h"

#include "my_classes/Name.hpp"

namespace stellar
{

using namespace std;

TEST_CASE("toXdr", "[overlay][PeerManager]")
{
    VirtualClock clock;
    Application::pointer app = createTestApplication(clock, getTestConfig());
    auto& pm = app->getOverlayManager().getPeerManager();
    auto address = my::PeerName("00000256");

    SECTION("toXdr")
    {
        string peerName{"00000256"};
        REQUIRE(address.toString() == peerName);


        auto xdr = toXdr(address);
        REQUIRE(xdr.data == *(uint64_t*)peerName.data());
    }

    SECTION("database roundtrip")
    {
        auto test = [&](PeerType peerType) {
            auto loadedPR = pm.load(address);
            REQUIRE(!loadedPR.second);

            auto storedPr = loadedPR.first;
            storedPr.mType = static_cast<int>(peerType);
            pm.store(address, storedPr, false);

            auto actualPR = pm.load(address);
            REQUIRE(actualPR.second);
            REQUIRE(actualPR.first == storedPr);
        };

        SECTION("inbound")
        {
            test(PeerType::INBOUND);
        }

        SECTION("outbound")
        {
            test(PeerType::OUTBOUND);
        }

        SECTION("preferred")
        {
            test(PeerType::PREFERRED);
        }
    }
}

TEST_CASE("loadRandomPeers", "[overlay][PeerManager]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    auto& peerManager = app->getOverlayManager().getPeerManager();

    auto getPeerNames = [&](PeerQuery const& query) {
        auto peers = peerManager.loadRandomPeers(query, 1000);
        auto result = std::set<my::PeerName>{};
        std::transform(
            std::begin(peers), std::end(peers),
            std::inserter(result, std::end(result)),
            [](my::PeerName const& address) { return address; });
        return result;
    };

    auto now = clock.now();
    auto past = clock.now() - std::chrono::seconds(1);
    auto future = clock.now() + std::chrono::seconds(1);

    my::PeerName port{"00000000"};
    auto peerRecords = std::map<my::PeerName, PeerRecord>{};
    for (auto time : {past, now, future})
    {
        for (auto numFailures : {0, 1})
        {
            for (auto type :
                 {PeerType::INBOUND, PeerType::OUTBOUND, PeerType::PREFERRED})
            {
                auto peerRecord =
                    PeerRecord{VirtualClock::pointToTm(time), numFailures,
                               static_cast<int>(type)};
                peerRecords[port] = peerRecord;
                peerManager.store(port, peerRecord, false);
                port++;
            }
        }
    }

    auto valid = [&](PeerQuery const& peerQuery, PeerRecord const& peerRecord) {
        if (peerQuery.mUseNextAttempt)
        {
            if (VirtualClock::tmToPoint(peerRecord.mNextAttempt) > now)
            {
                return false;
            }
        }
        if (peerQuery.mMaxNumFailures >= 0)
        {
            if (peerRecord.mNumFailures > peerQuery.mMaxNumFailures)
            {
                return false;
            }
        }
        switch (peerQuery.mTypeFilter)
        {
        case PeerTypeFilter::INBOUND_ONLY:
        {
            return peerRecord.mType == static_cast<int>(PeerType::INBOUND);
        }
        case PeerTypeFilter::OUTBOUND_ONLY:
        {
            return peerRecord.mType == static_cast<int>(PeerType::OUTBOUND);
        }
        case PeerTypeFilter::PREFERRED_ONLY:
        {
            return peerRecord.mType == static_cast<int>(PeerType::PREFERRED);
        }
        case PeerTypeFilter::ANY_OUTBOUND:
        {
            return peerRecord.mType == static_cast<int>(PeerType::OUTBOUND) ||
                   peerRecord.mType == static_cast<int>(PeerType::PREFERRED);
        }
        default:
        {
            abort();
        }
        }
    };

    for (auto useNextAttempt : {false, true})
    {
        for (auto numFailures : {-1, 0})
        {
            for (auto filter :
                 {PeerTypeFilter::INBOUND_ONLY, PeerTypeFilter::OUTBOUND_ONLY,
                  PeerTypeFilter::PREFERRED_ONLY, PeerTypeFilter::ANY_OUTBOUND})
            {
                auto query = PeerQuery{useNextAttempt, numFailures, filter};
                auto ports = getPeerNames(query);
                for (auto record : peerRecords)
                {
                    if (ports.find(record.first) != std::end(ports))
                    {
                        REQUIRE(valid(query, record.second));
                    }
                    else
                    {
                        REQUIRE(!valid(query, record.second));
                    }
                }
            }
        }
    }
}

TEST_CASE("getPeersToSend", "[overlay][PeerManager]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    auto& peerManager = app->getOverlayManager().getPeerManager();
    my::PeerName myAddress("00000001");
    auto getSize = [&](int requestedSize) {
        return peerManager.getPeersToSend(requestedSize, myAddress).size();
    };
    auto createPeers = [&](unsigned short normalInboundCount,
                           unsigned short failedInboundCount,
                           unsigned short normalOutboundCount,
                           unsigned short failedOutboundCount) {
        my::PeerName port{"00000001"};
        for (auto i = 0; i < normalInboundCount; i++)
        {
            peerManager.ensureExists(port++);
        }
        for (auto i = 0; i < failedInboundCount; i++)
        {
            peerManager.store(
                    port++,
                PeerRecord{{}, 11, static_cast<int>(PeerType::INBOUND)}, false);
        }
        for (auto i = 0; i < normalOutboundCount; i++)
        {
            peerManager.update(port++,
                               PeerManager::TypeUpdate::SET_OUTBOUND);
        }
        for (auto i = 0; i < failedOutboundCount; i++)
        {
            peerManager.store(
                port++,
                PeerRecord{{}, 11, static_cast<int>(PeerType::OUTBOUND)},
                false);
        }
    };

    SECTION("no peers in database")
    {
        REQUIRE(getSize(0) == 0);
        REQUIRE(getSize(10) == 0);
        REQUIRE(getSize(50) == 0);
    }

    SECTION("less peers in database than requested")
    {
        SECTION("only inbound peers")
        {
            createPeers(8, 0, 0, 0);
            REQUIRE(getSize(10) == 8);
            REQUIRE(getSize(50) == 8);
        }
        SECTION("only outbound peers")
        {
            createPeers(0, 0, 8, 0);
            REQUIRE(getSize(10) == 8);
            REQUIRE(getSize(50) == 8);
        }
        SECTION("mixed peers")
        {
            createPeers(4, 0, 4, 0);
            REQUIRE(getSize(10) == 8);
            REQUIRE(getSize(50) == 8);
        }
    }

    SECTION("as many peers in database as requested")
    {
        SECTION("only inbound peers")
        {
            createPeers(8, 0, 0, 0);
            REQUIRE(getSize(8) == 8);
        }
        SECTION("only outbound peers")
        {
            createPeers(0, 0, 8, 0);
            REQUIRE(getSize(8) == 8);
        }
        SECTION("mixed peers")
        {
            createPeers(4, 0, 4, 0);
            REQUIRE(getSize(8) == 8);
        }
    }

    SECTION("more peers in database than requested")
    {
        SECTION("only inbound peers")
        {
            createPeers(50, 0, 0, 0);
            REQUIRE(getSize(30) == 30);
        }
        SECTION("only outbound peers")
        {
            createPeers(0, 0, 50, 0);
            REQUIRE(getSize(30) == 30);
        }
        SECTION("mixed peers")
        {
            createPeers(25, 0, 25, 0);
            REQUIRE(getSize(30) == 30);
        }
    }

    SECTION("more peers in database than requested, but half failed")
    {
        SECTION("only inbound peers")
        {
            createPeers(25, 25, 0, 0);
            REQUIRE(getSize(30) == 25);
        }
        SECTION("only outbound peers")
        {
            createPeers(0, 0, 25, 25);
            REQUIRE(getSize(30) == 25);
        }
        SECTION("mixed peers")
        {
            createPeers(13, 12, 13, 12);
            REQUIRE(getSize(30) == 26);
        }
    }
}

TEST_CASE("RandomPeerSource::nextAttemptCutoff also limits maxFailures",
          "[overlay][PeerManager]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    auto& peerManager = app->getOverlayManager().getPeerManager();
    auto randomPeerSource = RandomPeerSource{
        peerManager, RandomPeerSource::nextAttemptCutoff(PeerType::OUTBOUND)};

    auto now = VirtualClock::pointToTm(clock.now());
    peerManager.store(my::PeerName("00000001"),
                      {now, 0, static_cast<int>(PeerType::INBOUND)}, false);
    peerManager.store(my::PeerName("00000002"),
                      {now, 0, static_cast<int>(PeerType::OUTBOUND)}, false);
    peerManager.store(my::PeerName("00000003"),
                      {now, 120, static_cast<int>(PeerType::INBOUND)}, false);
    peerManager.store(my::PeerName("00000004"),
                      {now, 120, static_cast<int>(PeerType::OUTBOUND)}, false);
    peerManager.store(my::PeerName("00000005"),
                      {now, 121, static_cast<int>(PeerType::INBOUND)}, false);
    peerManager.store(my::PeerName("00000006"),
                      {now, 121, static_cast<int>(PeerType::OUTBOUND)}, false);

    auto peers = randomPeerSource.getRandomPeers(
        50, [](my::PeerName const&) { return true; });
    REQUIRE(peers.size() == 2);
    REQUIRE(std::find(std::begin(peers), std::end(peers), my::PeerName("00000002")) !=
            std::end(peers));
    REQUIRE(std::find(std::begin(peers), std::end(peers), my::PeerName("00000004")) !=
            std::end(peers));
}

TEST_CASE("purge peer table", "[overlay][PeerManager]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    auto& peerManager = app->getOverlayManager().getPeerManager();
    auto record = [](int numFailures) {
        return PeerRecord{{}, numFailures, static_cast<int>(PeerType::INBOUND)};
    };

    peerManager.store(my::PeerName("00000001"), record(1), false);
    peerManager.store(my::PeerName("00000002"), record(2), false);
    peerManager.store(my::PeerName("00000003"), record(3), false);
    peerManager.store(my::PeerName("00000004"), record(4), false);
    peerManager.store(my::PeerName("00000005"), record(5), false);

    peerManager.removePeersWithManyFailures(3);
    REQUIRE(peerManager.load(my::PeerName("00000001")).second);
    REQUIRE(peerManager.load(my::PeerName("00000002")).second);
    REQUIRE(!peerManager.load(my::PeerName("00000003")).second);
    REQUIRE(!peerManager.load(my::PeerName("00000004")).second);
    REQUIRE(!peerManager.load(my::PeerName("00000005")).second);

    auto localhost2 = my::PeerName("00000002");
    peerManager.removePeersWithManyFailures(3, &localhost2);
    REQUIRE(peerManager.load(my::PeerName("00000002")).second);

    peerManager.removePeersWithManyFailures(2, &localhost2);
    REQUIRE(!peerManager.load(my::PeerName("00000002")).second);
}
}
