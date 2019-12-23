//// Copyright 2014 Stellar Development Foundation and contributors. Licensed
//// under the Apache License, Version 2.0. See the COPYING file at the root
//// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
//
//#include "util/asio.h"
//#include "main/ApplicationImpl.h"
//#include "main/Config.h"
//
//#include "database/Database.h"
//#include "lib/catch.hpp"
//#include "overlay/OverlayManager.h"
//#include "overlay/OverlayManagerImpl.h"
//#include "test/TestAccount.h"
//#include "test/TestUtils.h"
//#include "test/TxTests.h"
//#include "test/test.h"
//#include "transactions/TransactionFrame.h"
//#include "util/Timer.h"
//
//#include <soci.h>
//
//using namespace stellar;
//using namespace std;
//using namespace soci;
//using namespace txtest;
//
//static const my::AppName LOCALHOST_ANALOG{"00000000"};
//
//namespace stellar
//{
//
//class PeerStub : public Peer
//{
//  public:
//    int sent = 0;
//
//    PeerStub(Application& app, my::FullName peerFullName, my::FullName myName)
//        : Peer(app, WE_CALLED_REMOTE, move(peerFullName), move(myName))
//    {
//        mPeerID = SecretKey::pseudoRandomForTesting().getPublicKey();
//        mState = GOT_AUTH;
//    }
//    my::AppName const&
//    getAppName() const override
//    {
//        REQUIRE(false); // should not be called
//        return {};
//    }
//    virtual void
//    drop(std::string const&, DropDirection, DropMode) override
//    {
//    }
//    virtual void
//    sendMessage(xdr::msg_ptr&& xdrBytes) override
//    {
//        sent++;
//    }
//};
//
//class OverlayManagerStub : public OverlayManagerImpl
//{
//  public:
//    OverlayManagerStub(Application& app) : OverlayManagerImpl(app)
//    {
//    }
//
//    virtual bool
//    connectToImpl(my::FullName const& name, bool) override
//    {
//        if (getConnectedPeer(name))
//        {
//            return false;
//        }
//
//        getPeerManager().update(name, PeerManager::BackOffUpdate::INCREASE);
//
//        auto peerStub = std::make_shared<PeerStub>(mApp, name, my::FullName(LOCALHOST_ANALOG));
//        REQUIRE(addOutboundConnection(peerStub));
//        return acceptAuthenticatedPeer(peerStub);
//    }
//};
//
//class OverlayManagerTests
//{
//    class ApplicationStub : public TestApplication
//    {
//      public:
//        ApplicationStub(VirtualClock& clock, Config const& cfg)
//            : TestApplication(clock, cfg)
//        {
//        }
//
//        virtual OverlayManagerStub&
//        getOverlayManager() override
//        {
//            auto& overlay = ApplicationImpl::getOverlayManager();
//            return static_cast<OverlayManagerStub&>(overlay);
//        }
//
//      private:
//        virtual std::unique_ptr<OverlayManager>
//        createOverlayManager() override
//        {
//            return std::make_unique<OverlayManagerStub>(*this);
//        }
//    };
//
//  protected:
//    VirtualClock clock;
//    std::shared_ptr<ApplicationStub> app;
//
//    std::vector<my::FullName> fourPeers;
//    std::vector<my::FullName> threePeers;
//
//    OverlayManagerTests()
//        : fourPeers(std::vector<my::FullName>{
//            my::FullName(LOCALHOST_ANALOG, my::PeerName("00002011")),
//            my::FullName(LOCALHOST_ANALOG, my::PeerName("00002012")),
//            my::FullName(LOCALHOST_ANALOG, my::PeerName("00002013")),
//            my::FullName(LOCALHOST_ANALOG, my::PeerName("00002014"))})
//        , threePeers(std::vector<my::FullName>{
//                    my::FullName(LOCALHOST_ANALOG, my::PeerName("00064000")),
//                    my::FullName(LOCALHOST_ANALOG, my::PeerName("00064001")),
//                    my::FullName(LOCALHOST_ANALOG, my::PeerName("00064002"))})
//    {
//        auto cfg = getTestConfig();
//        cfg.TARGET_PEER_CONNECTIONS = 5;
//        cfg.KNOWN_PEERS = threePeers;
//        cfg.PREFERRED_PEERS = fourPeers;
//        app = createTestApplication<ApplicationStub>(clock, cfg);
//    }
//
//    void
//    testAddPeerList(bool async = false)
//    {
//        OverlayManagerStub& pm = app->getOverlayManager();
//
//        if (async)
//        {
//            pm.triggerPeerResolution();
//            REQUIRE(pm.mResolvedPeers.valid());
//            pm.mResolvedPeers.wait();
//
//            // Start ticking to store resolved peers
//            pm.tick();
//        }
//        else
//        {
//            pm.storeConfigPeers();
//        }
//
//        rowset<row> rs = app->getDatabase().getSession().prepare
//                         << "SELECT appName,peerName,type FROM peers ORDER BY appName, peerName";
//
//        auto& ppeers = pm.mConfigurationPreferredPeers;
//        int i = 0;
//        for (auto it = rs.begin(); it != rs.end(); ++it, ++i)
//        {
//
//            my::FullName pba{it->get<std::string>(0), it->get<std::string>(1)};
//            auto type = it->get<int>(2);
//            if (i < fourPeers.size())
//            {
//                REQUIRE(fourPeers[i] == pba.toString());
//                REQUIRE(ppeers.find(pba) != ppeers.end());
//                REQUIRE(type == static_cast<int>(PeerType::PREFERRED));
//            }
//            else
//            {
//                REQUIRE(threePeers[i - fourPeers.size()] == pba.toString());
//                REQUIRE(type == static_cast<int>(PeerType::OUTBOUND));
//            }
//        }
//        REQUIRE(i == (threePeers.size() + fourPeers.size()));
//    }
//
//    void
//    testAddPeerListUpdateType()
//    {
//        // This test case assumes peer was discovered prior to
//        // resolution, and makes sure peer type is properly updated
//        // (from INBOUND to OUTBOUND)
//
//        OverlayManagerStub& pm = app->getOverlayManager();
////        PeerBareAddress prefPba{"127.0.0.1", 2011};
////        PeerBareAddress pba{"127.0.0.1", 64000};
//        my::FullName prefPba{LOCALHOST_ANALOG, my::PeerName("00002011")};
//        my::FullName pba{LOCALHOST_ANALOG, my::PeerName("00064000")};
//
//
//        auto prefPr = pm.getPeerManager().load(prefPba);
//        auto pr = pm.getPeerManager().load(pba);
//
//        REQUIRE(prefPr.first.mType == static_cast<int>(PeerType::INBOUND));
//        REQUIRE(pr.first.mType == static_cast<int>(PeerType::INBOUND));
//
//        pm.triggerPeerResolution();
//        REQUIRE(pm.mResolvedPeers.valid());
//        pm.mResolvedPeers.wait();
//        pm.tick();
//
//        rowset<row> rs = app->getDatabase().getSession().prepare
//                         << "SELECT appName,peerName,type FROM peers ORDER BY appName, peerName";
//
//        int found = 0;
//        for (auto it = rs.begin(); it != rs.end(); ++it)
//        {
//            my::FullName storedPba{it->get<std::string>(0),it->get<std::string>(1)};
//            auto type = it->get<int>(2);
//            if (storedPba == pba)
//            {
//                ++found;
//                REQUIRE(type == static_cast<int>(PeerType::OUTBOUND));
//            }
//            else if (storedPba == prefPba)
//            {
//                ++found;
//                REQUIRE(type == static_cast<int>(PeerType::PREFERRED));
//            }
//        }
//        REQUIRE(found == 2);
//    }
//
//    std::vector<int>
//    sentCounts(OverlayManagerImpl& pm)
//    {
//        std::vector<int> result;
//        for (auto p : pm.mInboundPeers.mAuthenticated)
//            result.push_back(static_pointer_cast<PeerStub>(p.second)->sent);
//        for (auto p : pm.mOutboundPeers.mAuthenticated)
//            result.push_back(static_pointer_cast<PeerStub>(p.second)->sent);
//        return result;
//    }
//
//    void
//    testBroadcast()
//    {
//        OverlayManagerStub& pm = app->getOverlayManager();
//
//        auto fourPeersAddresses = pm.resolvePeers(fourPeers);
//        auto threePeersAddresses = pm.resolvePeers(threePeers);
//        pm.storePeerList(fourPeersAddresses, false, true);
//        pm.storePeerList(threePeersAddresses, false, true);
//
//        // connect to peers, respecting TARGET_PEER_CONNECTIONS
//        pm.tick();
//        REQUIRE(pm.mInboundPeers.mAuthenticated.size() == 0);
//        REQUIRE(pm.mOutboundPeers.mAuthenticated.size() == 5);
//        auto a = TestAccount{*app, getAccount("a")};
//        auto b = TestAccount{*app, getAccount("b")};
//        auto c = TestAccount{*app, getAccount("c")};
//        auto d = TestAccount{*app, getAccount("d")};
//
//        StellarMessage AtoC = a.tx({payment(b, 10)})->toStellarMessage();
//        auto i = 0;
//        for (auto p : pm.mOutboundPeers.mAuthenticated)
//            if (i++ == 2)
//                pm.recvFloodedMsg(AtoC, p.second);
//        pm.broadcastMessage(AtoC);
//        std::vector<int> expected{1, 1, 0, 1, 1};
//        REQUIRE(sentCounts(pm) == expected);
//        pm.broadcastMessage(AtoC);
//        REQUIRE(sentCounts(pm) == expected);
//        StellarMessage CtoD = c.tx({payment(d, 10)})->toStellarMessage();
//        pm.broadcastMessage(CtoD);
//        std::vector<int> expectedFinal{2, 2, 1, 2, 2};
//        REQUIRE(sentCounts(pm) == expectedFinal);
//    }
//};
//
//TEST_CASE_METHOD(OverlayManagerTests, "storeConfigPeers() adds", "[overlay]")
//{
//    testAddPeerList(false);
//}
//
//TEST_CASE_METHOD(OverlayManagerTests,
//                 "triggerPeerResolution() async resolution", "[overlay]")
//{
//    testAddPeerList(true);
//}
//
//TEST_CASE_METHOD(OverlayManagerTests, "storeConfigPeers() update type",
//                 "[overlay]")
//{
//    testAddPeerListUpdateType();
//}
//
//TEST_CASE_METHOD(OverlayManagerTests, "broadcast() broadcasts", "[overlay]")
//{
//    testBroadcast();
//}
//}
