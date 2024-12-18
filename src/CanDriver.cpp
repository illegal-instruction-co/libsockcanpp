/**
 * @file CanDriver.cpp
 * @author Simon Cahill (simonc@online.de)
 * @brief
 * @version 0.1
 * @date 2020-07-01
 *
 * @copyright Copyright (c) 2020 Simon Cahill
 *
 *
 *  Copyright 2020 Simon Cahill
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "CanDriver.hpp"
#include "CanId.hpp"
#include "CanMessage.hpp"

#include "exceptions/CanCloseException.hpp"
#include "exceptions/CanException.hpp"
#include "exceptions/CanInitException.hpp"
#include "exceptions/InvalidSocketException.hpp"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace sockcanpp {

using exceptions::CanCloseException;
using exceptions::CanException;
using exceptions::CanInitException;
using exceptions::InvalidSocketException;

using std::mutex;
using std::queue;
using std::string;
using std::string_view;
using std::strncpy;
using std::unique_lock;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

const int32_t CanDriver::CAN_MAX_DATA_LENGTH = 8;
const int32_t CanDriver::CAN_SOCK_RAW = CAN_RAW;
const int32_t CanDriver::CAN_SOCK_SEVEN = 7;

#pragma region Object Construction
CanDriver::CanDriver(string_view canInterface, int32_t canProtocol,
                     const CanId defaultSenderId)
    : CanDriver(canInterface, canProtocol, 0 /* match all */, defaultSenderId) {
}

CanDriver::CanDriver(const string_view canInterface, const int32_t canProtocol,
                     const int32_t filterMask, const CanId defaultSenderId)
    : _defaultSenderId(defaultSenderId), _canProtocol(canProtocol),
      _canInterface(canInterface), _canFilterMask(filterMask), _socketFd(-1) {
  initialiseSocketCan();
}
#pragma endregion

#pragma region I / O

bool CanDriver::waitForMessages(milliseconds timeout) {
  if (_socketFd < 0)
    throw InvalidSocketException("Invalid socket!", _socketFd);

  unique_lock<mutex> locky(_lock);

  fd_set readFileDescriptors;
  timeval waitTime;
  waitTime.tv_sec = timeout.count() / 1000;
  waitTime.tv_usec = timeout.count() * 1000;

  FD_ZERO(&readFileDescriptors);
  FD_SET(_socketFd, &readFileDescriptors);
  _queueSize = select(_socketFd + 1, &readFileDescriptors, 0, 0, &waitTime);

  return _queueSize > 0;
}

CanMessage CanDriver::readMessage() { return readMessageLock(); }

CanMessage CanDriver::readMessageLock(bool const lock) {
  std::unique_ptr<std::unique_lock<std::mutex>> _lockLck{nullptr};
  if (lock)
    _lockLck = std::unique_ptr<std::unique_lock<std::mutex>>{
        new std::unique_lock<std::mutex>{_lock}};
  if (0 > _socketFd)
    throw InvalidSocketException("Invalid socket!", _socketFd);
  int32_t readBytes{0};
  can_frame canFrame;
  memset(&canFrame, 0, sizeof(can_frame));
  readBytes = read(_socketFd, &canFrame, sizeof(can_frame));
  if (0 > readBytes)
    throw CanException(formatString("FAILED to read from CAN! Error: %d => %s",
                                    errno, strerror(errno)),
                       _socketFd);
  return CanMessage{canFrame};
}

int32_t CanDriver::sendMessage(const CanMessage message, bool forceExtended) {
  if (_socketFd < 0)
    throw InvalidSocketException("Invalid socket!", _socketFd);

  unique_lock<mutex> locky(_lockSend);

  int32_t bytesWritten = 0;

  if (message.getFrameData().size() > CAN_MAX_DATA_LENGTH)
    throw CanException(
        formatString(
            "INVALID data length! Message must be smaller than %d bytes!",
            CAN_MAX_DATA_LENGTH),
        _socketFd);

  auto canFrame = message.getRawFrame();

  if (forceExtended || ((uint32_t)message.getCanId() > CAN_SFF_MASK))
    canFrame.can_id |= CAN_EFF_FLAG;

  bytesWritten = write(_socketFd, (const void *)&canFrame, sizeof(canFrame));

  if (bytesWritten == -1)
    throw CanException(
        formatString("FAILED to write data to socket! Error: %d => %s", errno,
                     strerror(errno)),
        _socketFd);

  return bytesWritten;
}

int32_t CanDriver::sendMessageQueue(queue<CanMessage> messages,
                                    milliseconds delay, bool forceExtended) {
  if (_socketFd < 0)
    throw InvalidSocketException("Invalid socket!", _socketFd);

  int32_t totalBytesWritten = 0;

  while (!messages.empty()) {
    totalBytesWritten += sendMessage(messages.front(), forceExtended);
    messages.pop();
  }

  return totalBytesWritten;
}

queue<CanMessage> CanDriver::readQueuedMessages() {
  if (_socketFd < 0)
    throw InvalidSocketException("Invalid socket!", _socketFd);
  unique_lock<mutex> locky(_lock);
  queue<CanMessage> messages;
  for (int32_t i = _queueSize; 0 < i; --i)
    messages.emplace(readMessageLock(false));
  return messages;
}

void CanDriver::setCanFilterMask(const int32_t mask, const int32_t id) {
  if (_socketFd < 0)
    throw InvalidSocketException("Invalid socket!", _socketFd);

  unique_lock<mutex> locky(_lock);
  can_filter canFilter;

  if(id)
    canFilter.can_id = id;
  else
    canFilter.can_id = _defaultSenderId;
  
  canFilter.can_mask = mask;

  if (setsockopt(_socketFd, SOL_CAN_RAW, CAN_RAW_FILTER, &canFilter,
                 sizeof(canFilter)) == -1)
    throw CanInitException(formatString(
        "FAILED to set CAN filter mask %x on socket %d! Error: %d => %s", mask,
        _socketFd, errno, strerror(errno)));

  _canFilterMask = mask;
}
#pragma endregion

#pragma region Socket Management

void CanDriver::initialiseSocketCan() {
  struct sockaddr_can address;
  struct ifreq ifaceRequest;
  int64_t fdOptions = 0;
  int32_t tmpReturn;

  memset(&address, 0, sizeof(sizeof(struct sockaddr_can)));
  memset(&ifaceRequest, 0, sizeof(sizeof(struct ifreq)));

  _socketFd = socket(PF_CAN, SOCK_RAW, _canProtocol);

  if (_socketFd == -1)
    throw CanInitException(
        formatString("FAILED to initialise socketcan! Error: %d => %s", errno,
                     strerror(errno)));

  strcpy(ifaceRequest.ifr_name, _canInterface.data());

  if ((tmpReturn = ioctl(_socketFd, SIOCGIFINDEX, &ifaceRequest)) == -1)
    throw CanInitException(
        formatString("FAILED to perform IO control operation on socket %s! "
                     "Error: %d => %s",
                     _canInterface.data(), errno, strerror(errno)));

  fdOptions = fcntl(_socketFd, F_GETFL);
  fdOptions |= O_NONBLOCK;
  tmpReturn = fcntl(_socketFd, F_SETFL, fdOptions);

  address.can_family = AF_CAN;
  address.can_ifindex = ifaceRequest.ifr_ifindex;

  setCanFilterMask(_canFilterMask, _defaultSenderId);

  if ((tmpReturn =
           bind(_socketFd, (struct sockaddr *)&address, sizeof(address))) == -1)
    throw CanInitException(
        formatString("FAILED to bind to socket CAN! Error: %d => %s", errno,
                     strerror(errno)));
}

void CanDriver::uninitialiseSocketCan() {
  unique_lock<mutex> locky(_lock);

  if (_socketFd <= 0)
    throw CanCloseException("Cannot close invalid socket!");

  if (close(_socketFd) == -1)
    throw CanCloseException(formatString(
        "FAILED to close CAN socket! Error: %d => %s", errno, strerror(errno)));

  _socketFd = -1;
}
#pragma endregion

} // namespace sockcanpp
