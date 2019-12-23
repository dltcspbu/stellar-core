// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/PeerManager.h"
#include "crypto/Random.h"
#include "database/Database.h"
#include "main/Application.h"
#include "overlay/RandomPeerSource.h"
#include "overlay/StellarXDR.h"
#include "util/Logging.h"
#include "util/Math.h"
#include "util/must_use.h"

#include <algorithm>
#include <cmath>
#include <lib/util/format.h>
#include <regex>
#include <soci.h>
#include <vector>

namespace stellar {

  using namespace soci;

  enum PeerRecordFlags {
    PEER_RECORD_FLAGS_PREFERRED = 1
  };

  bool
  operator==(PeerRecord const &x, PeerRecord const &y) {
    if (VirtualClock::tmToPoint(x.mNextAttempt) !=
        VirtualClock::tmToPoint(y.mNextAttempt)) {
      return false;
    }
    if (x.mNumFailures != y.mNumFailures) {
      return false;
    }
    return x.mType == y.mType;
  }

  namespace {

    void
    ipToXdr(std::string const &ip, xdr::opaque_array<4U> &ret) {
      std::stringstream ss(ip);
      std::string item;
      int n = 0;
      while (getline(ss, item, '.') && n < 4) {
        ret[n] = static_cast<unsigned char>(atoi(item.c_str()));
        n++;
      }
      if (n != 4)
        throw std::runtime_error("ipToXdr: failed on `" + ip + "`");
    }
  }

  PeerNameXdr
  toXdr(my::PeerName const &peerName) {
    PeerNameXdr result{};
    result.data = *(uint64_t *) (peerName.toString().data());

    return result;
  }

  constexpr const auto BATCH_SIZE = 1000;
  constexpr const auto MAX_FAILURES = 10;

  PeerManager::PeerManager(Application &app)
      : mApp(app), mOutboundPeersToSend(std::make_unique<RandomPeerSource>(
      *this, RandomPeerSource::maxFailures(MAX_FAILURES, true))),
        mInboundPeersToSend(std::make_unique<RandomPeerSource>(
            *this, RandomPeerSource::maxFailures(MAX_FAILURES, false))) {
  }

  std::vector<my::PeerName>
  PeerManager::loadRandomPeers(PeerQuery const &query, int size) {
    // BATCH_SIZE should always be bigger, so it should win anyway
    size = std::max(size, BATCH_SIZE);

    // if we ever start removing peers from db, we may need to enable this
    // soci::transaction sqltx(mApp.getDatabase().getSession());
    // mApp.getDatabase().setCurrentTransactionReadOnly();

    std::vector<std::string> conditions;
    if (query.mUseNextAttempt) {
      conditions.push_back("nextattempt <= :nextattempt");
    }
    if (query.mMaxNumFailures >= 0) {
      conditions.push_back("numfailures <= :maxFailures");
    }
    if (query.mTypeFilter == PeerTypeFilter::ANY_OUTBOUND) {
      conditions.push_back("type != :inboundType");
    } else {
      conditions.push_back("type = :type");
    }
    assert(!conditions.empty());
    std::string where = conditions[0];
    for (auto i = 1; i < conditions.size(); i++) {
      where += " AND " + conditions[i];
    }

    std::tm nextAttempt = VirtualClock::pointToTm(mApp.getClock().now());
    int maxNumFailures = query.mMaxNumFailures;
    int exactType = static_cast<int>(query.mTypeFilter);
    int inboundType = static_cast<int>(PeerType::INBOUND);

    auto bindToStatement = [&](soci::statement &st) {
      if (query.mUseNextAttempt) {
        st.exchange(soci::use(nextAttempt));
      }
      if (query.mMaxNumFailures >= 0) {
        st.exchange(soci::use(maxNumFailures));
      }
      if (query.mTypeFilter == PeerTypeFilter::ANY_OUTBOUND) {
        st.exchange(soci::use(inboundType));
      } else {
        st.exchange(soci::use(exactType));
      }
    };

    auto result = std::vector<my::PeerName>{};
    auto count = countPeers(where, bindToStatement);
    if (count == 0) {
      return result;
    }

    auto maxOffset = count > size ? count - size : 0;
    auto offset = rand_uniform<int>(0, maxOffset);
    result = loadPeers(size, offset, where, bindToStatement);

    std::shuffle(std::begin(result), std::end(result), gRandomEngine);
    return result;
  }

  void
  PeerManager::removePeersWithManyFailures(int minNumFailures,
                                           my::PeerName const *peerName) {
    try {
      auto &db = mApp.getDatabase();
      auto sql = std::string{
          "DELETE FROM peers WHERE numfailures >= :minNumFailures"};
      if (peerName) {
        sql += " AND peerName = :peerName";
      }

      auto prep = db.getPreparedStatement(sql);
      auto &st = prep.statement();

      st.exchange(use(minNumFailures));

      if (peerName) {
        st.exchange(use(peerName->toString()));
      }
      st.define_and_bind();

      {
        auto timer = db.getDeleteTimer("peer");
        st.execute(true);
      }
    }
    catch (soci_error &err) {
      CLOG(ERROR, "Overlay")
          << "PeerManager::removePeersWithManyFailures error: " << err.what();
    }
  }

  std::vector<my::PeerName>
  PeerManager::getPeersToSend(int size, my::PeerName const &peerName) {
    auto keep = [&](my::PeerName const &pba) {
      //std::cout << pba.toString() << " " << peerName.toString() << " " << (pba != peerName) << std::endl;
      return pba != peerName;
    };

    auto peers = mOutboundPeersToSend->getRandomPeers(size, keep);
    if (peers.size() < size) {
      auto inbound = mInboundPeersToSend->getRandomPeers(
          size - static_cast<int>(peers.size()), keep);
      std::copy(std::begin(inbound), std::end(inbound),
                std::back_inserter(peers));
    }

    return peers;
  }

  std::pair<PeerRecord, bool>
  PeerManager::load(my::PeerName const &peerName) {
    auto result = PeerRecord{};
    auto inDatabase = false;

    try {
      auto prep = mApp.getDatabase().getPreparedStatement(
          "SELECT numfailures, nextattempt, type FROM peers "
          "WHERE peerName = :v1");
      auto &st = prep.statement();
      st.exchange(into(result.mNumFailures));
      st.exchange(into(result.mNextAttempt));
      st.exchange(into(result.mType));
      st.exchange(use(peerName.toString()));
      st.define_and_bind();
      {
        auto timer = mApp.getDatabase().getSelectTimer("peer");
        st.execute(true);
        inDatabase = st.got_data();

        if (!inDatabase) {
          result.mNextAttempt =
              VirtualClock::pointToTm(mApp.getClock().now());
          result.mType = static_cast<int>(PeerType::INBOUND);
        }
      }
    }
    catch (soci_error &err) {
      CLOG(ERROR, "Overlay") << "PeerManager::load error: " << err.what()
                             << " on " << peerName.toString();
    }

    return std::make_pair(result, inDatabase);
  }

  void
  PeerManager::store(my::PeerName const &peerName, PeerRecord const &peerRecord,
                     bool inDatabase) {
    std::string query;

    if (inDatabase) {
      query = "UPDATE peers SET "
              "nextattempt = :v1, "
              "numfailures = :v2, "
              "type = :v3 "
              "WHERE peerName = :v4";
    } else {
      query = "INSERT INTO peers "
              "(nextattempt, numfailures, type, peerName) "
              "VALUES "
              "(:v1,         :v2,        :v3,  :v4)";
    }

    try {
      auto prep = mApp.getDatabase().getPreparedStatement(query);
      auto &st = prep.statement();
      st.exchange(use(peerRecord.mNextAttempt));
      st.exchange(use(peerRecord.mNumFailures));
      st.exchange(use(peerRecord.mType));
      st.exchange(use(peerName.toString()));
      st.define_and_bind();
      {
        auto timer = mApp.getDatabase().getUpdateTimer("peer");
        st.execute(true);
        if (st.get_affected_rows() != 1) {
          CLOG(ERROR, "Overlay")
              << "PeerManager::store failed on " + peerName.toString();
        }
      }
    }
    catch (soci_error &err) {
      CLOG(ERROR, "Overlay") << "PeerManager::store error: " << err.what()
                             << " on " << peerName.toString();
    }
  }

  void
  PeerManager::update(PeerRecord &peer, TypeUpdate type) {
    switch (type) {
      case TypeUpdate::SET_OUTBOUND: {
        peer.mType = static_cast<int>(PeerType::OUTBOUND);
        break;
      }
      case TypeUpdate::SET_PREFERRED: {
        peer.mType = static_cast<int>(PeerType::PREFERRED);
        break;
      }
      case TypeUpdate::REMOVE_PREFERRED: {
        if (peer.mType == static_cast<int>(PeerType::PREFERRED)) {
          peer.mType = static_cast<int>(PeerType::OUTBOUND);
        }
        break;
      }
      case TypeUpdate::UPDATE_TO_OUTBOUND: {
        if (peer.mType == static_cast<int>(PeerType::INBOUND)) {
          peer.mType = static_cast<int>(PeerType::OUTBOUND);
        }
        break;
      }
      default: {
        abort();
      }
    }
  }

  namespace {

    static std::chrono::seconds
    computeBackoff(int numFailures) {
      constexpr const auto SECONDS_PER_BACKOFF = 10;
      constexpr const auto MAX_BACKOFF_EXPONENT = 10;

      auto backoffCount = std::min<int32_t>(MAX_BACKOFF_EXPONENT, numFailures);
      auto nsecs = std::chrono::seconds(
          std::rand() % int(std::pow(2, backoffCount) * SECONDS_PER_BACKOFF) + 1);
      return nsecs;
    }
  }

  void
  PeerManager::update(PeerRecord &peer, BackOffUpdate backOff, Application &app) {
    switch (backOff) {
      case BackOffUpdate::HARD_RESET: {
        peer.mNumFailures = 0;
        auto nextAttempt = app.getClock().now();
        peer.mNextAttempt = VirtualClock::pointToTm(nextAttempt);
        break;
      }
      case BackOffUpdate::RESET:
      case BackOffUpdate::INCREASE: {
        peer.mNumFailures =
            backOff == BackOffUpdate::RESET ? 0 : peer.mNumFailures + 1;
        auto nextAttempt =
            app.getClock().now() + computeBackoff(peer.mNumFailures);
        peer.mNextAttempt = VirtualClock::pointToTm(nextAttempt);
        break;
      }
      default: {
        abort();
      }
    }
  }

  void
  PeerManager::ensureExists(my::PeerName const &peerName) {
    auto peer = load(peerName);
    if (!peer.second) {
      CLOG(TRACE, "Overlay") << "Learned peer " << peerName.toString() << " @"
                             << mApp.getConfig().PEER_NAME.toString();
      store(peerName, peer.first, peer.second);
    }
  }

  void
  PeerManager::update(my::PeerName const &peerName, TypeUpdate type) {
    auto peer = load(peerName);
    update(peer.first, type);
    store(peerName, peer.first, peer.second);
  }

  void
  PeerManager::update(my::PeerName const &peerName, BackOffUpdate backOff) {
    auto peer = load(peerName);
    update(peer.first, backOff, mApp);
    store(peerName, peer.first, peer.second);
  }

  void
  PeerManager::update(my::PeerName const &peerName, TypeUpdate type,
                      BackOffUpdate backOff) {
    auto peer = load(peerName);
    update(peer.first, type);
    update(peer.first, backOff, mApp);
    store(peerName, peer.first, peer.second);
  }

  int
  PeerManager::countPeers(std::string const &where,
                          std::function<void(soci::statement &)> const &bind) {
    int count = 0;

    try {
      std::string sql = "SELECT COUNT(*) FROM peers WHERE " + where;

      auto prep = mApp.getDatabase().getPreparedStatement(sql);
      auto &st = prep.statement();

      bind(st);
      st.exchange(into(count));

      st.define_and_bind();
      st.execute(true);
    }
    catch (soci_error &err) {
      CLOG(ERROR, "Overlay") << "countPeers error: " << err.what();
    }

    return count;
  }

  std::vector<my::PeerName>
  PeerManager::loadPeers(int limit, int offset, std::string const &where,
                         std::function<void(soci::statement &)> const &bind) {
    auto result = std::vector<my::PeerName>{};

    try {
      std::string sql = "SELECT peerName "
                        "FROM peers WHERE " +
                        where + " LIMIT :limit OFFSET :offset";

      auto prep = mApp.getDatabase().getPreparedStatement(sql);
      auto &st = prep.statement();

      bind(st);
      st.exchange(use(limit));
      st.exchange(use(offset));

      string peerNameStr;
      st.exchange(into(peerNameStr));

      st.define_and_bind();
      {
        auto timer = mApp.getDatabase().getSelectTimer("peer");
        st.execute(true);
      }
      while (st.got_data()) {
        if (not peerNameStr.empty()) {
          result.emplace_back(my::PeerName(peerNameStr));
        }
        st.fetch();
      }
    }
    catch (soci_error &err) {
      CLOG(ERROR, "Overlay") << "loadPeers error: " << err.what();
    }

    return result;
  }

  void
  PeerManager::dropAll(Database &db) {
    db.getSession() << "DROP TABLE IF EXISTS peers;";
    db.getSession() << kSQLCreateStatement;
  }

  const char *PeerManager::kSQLCreateStatement =
      "CREATE TABLE peers ("
      "peerName      VARCHAR(8) NOT NULL,"
      "nextattempt   TIMESTAMP NOT NULL,"
      "numfailures   INT DEFAULT 0 CHECK (numfailures >= 0) NOT NULL,"
      "type          INT NOT NULL,"
      "PRIMARY KEY (peerName)"
      ");";
}
