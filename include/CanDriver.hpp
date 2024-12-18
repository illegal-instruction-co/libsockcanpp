/**
 * @file CanDriver.hpp
 * @author Simon Cahill (simonc@online.de)
 * @brief Contains the declarations for the SocketCAN wrapper in C++.
 * @version 0.1
 * @date 2020-07-01
 *
 * @copyright Copyright (c) 2020
 *
 *  Copyright 2020 Simon Cahill
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "CanId.hpp"
#include "CanMessage.hpp"

#include <mutex>
#include <queue>
#include <string>
#include <string_view>

namespace sockcanpp {

using std::mutex;
using std::queue;
using std::string;
using std::string_view;
using std::chrono::milliseconds;

class CanDriver {
public:
  static const int32_t CAN_MAX_DATA_LENGTH;

  static const int32_t CAN_SOCK_RAW;
  static const int32_t CAN_SOCK_SEVEN;

public:
  CanDriver(const string_view, const int32_t,
            const CanId defaultSenderId = 0);
  CanDriver(const string_view, const int32_t canProtocol,
            const int32_t, const CanId defaultSenderId = 0);
  CanDriver() {}
  virtual ~CanDriver() { uninitialiseSocketCan(); }

public:
  CanDriver &setDefaultSenderId(const CanId id) {
    this->_defaultSenderId = id;
    return *this;
  }

  const CanId getDefaultSenderId() const { return this->_defaultSenderId; }

  const int32_t getFilterMask() const { return this->_canFilterMask; }
  const int32_t getMessageQueueSize() const { return this->_queueSize; }

  const int32_t getSocketFd() const { return this->_socketFd; }

public:
  virtual bool waitForMessages(milliseconds timeout = milliseconds(3000));

  virtual CanMessage readMessage();

  virtual int32_t sendMessage(const CanMessage,
                              bool forceExtended = false);
  virtual int32_t sendMessageQueue(queue<CanMessage> messages,
                                   milliseconds delay = milliseconds(20),
                                   bool forceExtended = false);

  virtual queue<CanMessage> readQueuedMessages();

  virtual void setCanFilterMask(const int32_t, const int32_t);

protected:
  virtual void initialiseSocketCan();
  virtual void uninitialiseSocketCan();

private:
  virtual CanMessage readMessageLock(bool const lock = true);
  CanId _defaultSenderId;

  int32_t _canFilterMask;
  int32_t _canProtocol;
  int32_t _socketFd;
  int32_t _queueSize;

  mutex _lock;
  mutex _lockSend;

  string_view _canInterface;
};

template <typename... Args>
string formatString(const string &format, Args... args) {
  using std::unique_ptr;
  auto stringSize = snprintf(NULL, 0, format.c_str(), args...) + 1;
  unique_ptr<char[]> buffer(new char[stringSize]);

  snprintf(buffer.get(), stringSize, format.c_str(), args...);

  return string(buffer.get(), buffer.get() + stringSize - 1);
}

} // namespace sockcanpp
