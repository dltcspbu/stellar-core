//
// Created by zharkov on 18.07.19.
//

#ifndef WEBRTCSTREAMER_MESSAGEBROKER_HPP
#define WEBRTCSTREAMER_MESSAGEBROKER_HPP

#include <string>
#include <vector>
#include <memory>

struct nodeConfig {
  std::string WSHost;
  int WSPort;
  std::string name;
  std::string user;
  std::string sqlite3Path;
  std::string outputStream;
  int openPortsStart;
  int openPortsEnd;
  std::string hashSeed;
};

struct roomIdsVector {
  std::vector<std::string> roomIds;
  unsigned int length;
};

class messageHandler {
public:
  messageHandler() {}

  virtual void handle(const std::string &m) = 0;

  virtual ~messageHandler() {}
};

class messageBroker {
public:
  messageBroker(nodeConfig _conf);

  ~messageBroker();

  void start();

  void addCallbackToRoom(const std::string &roomId, messageHandler *callback);

  bool removeCallbackFromRoom(const std::string &roomId, messageHandler *callback);

  void addCallbackToRoom(const std::string &roomId, std::shared_ptr<messageHandler> callback);

  bool removeCallbackFromRoom(const std::string &roomId, std::shared_ptr<messageHandler> callback);

  void joinRoom(const std::string &roomId);

  void send2client(const std::string &clientId, const std::string &roomId, const std::string &message);

  void broadcast2Room(const std::string &roomId, const std::string &message);

  void leaveRoom(const std::string &roomId);

  void exit();

  roomIdsVector getRoomIdsVector();

  nodeConfig getNodeConfig() const;

  std::vector<std::string> getNodeIds(const std::string &) const;

  static std::shared_ptr<messageBroker> Create(nodeConfig conf);

private:
  class signalerWrapper;

  nodeConfig _conf;
  signalerWrapper *api;
};

#endif //WEBRTCSTREAMER_MESSAGEBROKER_HPP
