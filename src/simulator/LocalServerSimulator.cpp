#include "../network/LocalServer.h"

LocalServer::LocalServer() = default;
LocalServer::~LocalServer() = default;
void LocalServer::begin() {}
void LocalServer::stop() {}
void LocalServer::handleClient() {}

LocalServer::WsUploadStatus LocalServer::getWsUploadStatus() const {
  return {};
}
