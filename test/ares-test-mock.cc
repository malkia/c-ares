/* MIT License
 *
 * Copyright (c) The c-ares project and its contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ares-test.h"
#include "dns-proto.h"

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <sstream>
#include <vector>

using testing::InvokeWithoutArgs;
using testing::DoAll;

namespace ares {
namespace test {

class NoDNS0x20MockTest
    : public MockChannelOptsTest,
      public ::testing::WithParamInterface<int> {
 public:
  NoDNS0x20MockTest()
    : MockChannelOptsTest(1, GetParam(), false, false,
                          FillOptions(&opts_),
                          ARES_OPT_FLAGS) {}
  static struct ares_options* FillOptions(struct ares_options * opts) {
    memset(opts, 0, sizeof(struct ares_options));
    opts->flags = ARES_FLAG_EDNS;
    return opts;
  }
 private:
  struct ares_options opts_;
};


TEST_P(NoDNS0x20MockTest, Basic) {
  std::vector<byte> reply = {
    0x00, 0x00,  // qid
    0x84, // response + query + AA + not-TC + not-RD
    0x00, // not-RA + not-Z + not-AD + not-CD + rc=NoError
    0x00, 0x01,  // 1 question
    0x00, 0x01,  // 1 answer RRs
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs
    // Question
    0x03, 'w', 'w', 'w',
    0x06, 'g', 'o', 'o', 'g', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x01,  // type A
    0x00, 0x01,  // class IN
    // Answer
    0x03, 'w', 'w', 'w',
    0x06, 'g', 'o', 'o', 'g', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x01,  // type A
    0x00, 0x01,  // class IN
    0x00, 0x00, 0x01, 0x00,  // TTL
    0x00, 0x04,  // rdata length
    0x01, 0x02, 0x03, 0x04
  };

  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReplyData(&server_, reply));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockUDPChannelTest, DNS0x20BadReply) {
  std::vector<byte> reply = {
    0x00, 0x00,  // qid
    0x84, // response + query + AA + not-TC + not-RD
    0x00, // not-RA + not-Z + not-AD + not-CD + rc=NoError
    0x00, 0x01,  // 1 question
    0x00, 0x01,  // 1 answer RRs
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs
    // Question
    0x03, 'w', 'w', 'w',
    0x1D, 's', 'o', 'm', 'e', 'l', 'o', 'n', 'g', 'd', 'o', 'm', 'a', 'i', 'n', 'n', 'a', 'm', 'e', 'b', 'e', 'c', 'a', 'u', 's', 'e', 'p', 'r', 'n', 'g',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x01,  // type A
    0x00, 0x01,  // class IN
    // Answer
    0x03, 'w', 'w', 'w',
    0x1D, 's', 'o', 'm', 'e', 'l', 'o', 'n', 'g', 'd', 'o', 'm', 'a', 'i', 'n', 'n', 'a', 'm', 'e', 'b', 'e', 'c', 'a', 'u', 's', 'e', 'p', 'r', 'n', 'g',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x01,  // type A
    0x00, 0x01,  // class IN
    0x00, 0x00, 0x01, 0x00,  // TTL
    0x00, 0x04,  // rdata length
    0x01, 0x02, 0x03, 0x04
  };

  ON_CALL(server_, OnRequest("www.somelongdomainnamebecauseprng.com", T_A))
    .WillByDefault(SetReplyData(&server_, reply));

  /* Reply will be thrown out due to mismatched case for DNS 0x20 in response,
   * its technically possible this test case may not fail if somehow the
   * PRNG returns all lowercase domain name so we need to make this domain
   * fairly long to make sure those odds are very very very low */
  HostResult result;
  ares_gethostbyname(channel_, "www.somelongdomainnamebecauseprng.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ETIMEOUT, result.status_);
}

// UDP only so mock server doesn't get confused by concatenated requests
TEST_P(MockUDPChannelTest, GetHostByNameParallelLookups) {
  DNSPacket rsp1;
  rsp1.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp1));
  DNSPacket rsp2;
  rsp2.set_response().set_aa()
    .add_question(new DNSQuestion("www.example.com", T_A))
    .add_answer(new DNSARR("www.example.com", 100, {1, 2, 3, 4}));
  ON_CALL(server_, OnRequest("www.example.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp2));

  HostResult result1;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result1);
  HostResult result2;
  ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result2);
  HostResult result3;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result3);
  Process();
  EXPECT_TRUE(result1.done_);
  EXPECT_TRUE(result2.done_);
  EXPECT_TRUE(result3.done_);
  std::stringstream ss1;
  ss1 << result1.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss1.str());
  std::stringstream ss2;
  ss2 << result2.host_;
  EXPECT_EQ("{'www.example.com' aliases=[] addrs=[1.2.3.4]}", ss2.str());
  std::stringstream ss3;
  ss3 << result3.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss3.str());
}

// UDP to TCP specific test
TEST_P(MockUDPChannelTest, TruncationRetry) {
  DNSPacket rsptruncated;
  rsptruncated.set_response().set_aa().set_tc()
    .add_question(new DNSQuestion("www.google.com", T_A));
  DNSPacket rspok;
  rspok.set_response()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {1, 2, 3, 4}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsptruncated))
    .WillOnce(SetReply(&server_, &rspok));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockUDPChannelTest, UTF8BadName) {
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("españa.icom.museum", T_A))
    .add_answer(new DNSARR("españa.icom.museum", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("españa.icom.museum", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  HostResult result;
  ares_gethostbyname(channel_, "españa.icom.museum", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_EBADNAME, result.status_);
}

static int sock_cb_count = 0;
static int SocketConnectCallback(ares_socket_t fd, int type, void *data) {
  int rc = *(int*)data;
  (void)type;
  if (verbose) std::cerr << "SocketConnectCallback(" << fd << ") invoked" << std::endl;
  sock_cb_count++;
  return rc;
}

TEST_P(MockChannelTest, SockCallback) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));

  // Get notified of new sockets
  int rc = ARES_SUCCESS;
  ares_set_socket_callback(channel_, SocketConnectCallback, &rc);

  HostResult result;
  sock_cb_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_EQ(1, sock_cb_count);
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, SockFailCallback) {
  // Notification of new sockets gives an error.
  int rc = -1;
  ares_set_socket_callback(channel_, SocketConnectCallback, &rc);

  HostResult result;
  sock_cb_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_LT(1, sock_cb_count);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ECONNREFUSED, result.status_);
}

static int sock_config_cb_count = 0;
static int SocketConfigureCallback(ares_socket_t fd, int type, void *data) {
  int rc = *(int*)data;
  (void)type;
  if (verbose) std::cerr << "SocketConfigureCallback(" << fd << ") invoked" << std::endl;
  sock_config_cb_count++;
  return rc;
}

TEST_P(MockChannelTest, SockConfigureCallback) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));

  // Get notified of new sockets
  int rc = ARES_SUCCESS;
  ares_set_socket_configure_callback(channel_, SocketConfigureCallback, &rc);

  HostResult result;
  sock_config_cb_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_EQ(1, sock_config_cb_count);
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, SockConfigureFailCallback) {
  // Notification of new sockets gives an error.
  int rc = -1;
  ares_set_socket_configure_callback(channel_, SocketConfigureCallback, &rc);

  HostResult result;
  sock_config_cb_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_LT(1, sock_config_cb_count);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ECONNREFUSED, result.status_);
}

// Define a server state callback for testing. The custom userdata should be
// the expected server string that the callback is invoked with.
static int server_state_cb_success_count = 0;
static int server_state_cb_failure_count = 0;
static void ServerStateCallback(const char *server_string,
                                ares_bool_t success, int flags, void *data) {
  // Increment overall success/failure counts appropriately.
  if (verbose) std::cerr << "ServerStateCallback("
                         << server_string << ", "
                         << success       << ", "
                         << flags         << ") invoked" << std::endl;
  if (success == ARES_TRUE) server_state_cb_success_count++;
  else server_state_cb_failure_count++;

  // Check that the server string is as expected.
  char *exp_server_string = *(char **)(data);
  EXPECT_STREQ(exp_server_string, server_string);

  // The callback should be invoked with either the UDP flag or the TCP flag,
  // but not both.
  ares_bool_t udp = (flags & ARES_SERV_STATE_UDP) ? ARES_TRUE: ARES_FALSE;
  ares_bool_t tcp = (flags & ARES_SERV_STATE_TCP) ? ARES_TRUE: ARES_FALSE;
  EXPECT_NE(udp, tcp);
}

TEST_P(MockChannelTest, ServStateCallbackSuccess) {
  // Set up the server response. The server returns successfully with an answer
  // to the query.
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));

  // Set up the server state callback. The channel used for this test has a
  // single server configured.
  char *exp_server_string = ares_get_servers_csv(channel_);
  ares_set_server_state_callback(channel_, ServerStateCallback,
                                 &exp_server_string);

  // Perform the hostname lookup. Expect 1 successful query to the server.
  HostResult result;
  server_state_cb_success_count = 0;
  server_state_cb_failure_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback,
                     &result);
  Process();
  EXPECT_EQ(1, server_state_cb_success_count);
  EXPECT_EQ(0, server_state_cb_failure_count);
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());

  ares_free_string(exp_server_string);
}

TEST_P(MockChannelTest, ServStateCallbackFailure) {
  // Set up the server response. The server always returns SERVFAIL.
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(SERVFAIL);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));

  // Set up the server state callback. The channel used for this test has a
  // single server configured.
  char *exp_server_string = ares_get_servers_csv(channel_);
  ares_set_server_state_callback(channel_, ServerStateCallback,
                                 &exp_server_string);

  // Perform the hostname lookup. Expect 3 failed queries to the server (due to
  // retries).
  HostResult result;
  server_state_cb_success_count = 0;
  server_state_cb_failure_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback,
                     &result);
  Process();
  EXPECT_EQ(0, server_state_cb_success_count);
  EXPECT_EQ(3, server_state_cb_failure_count);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ESERVFAIL, result.status_);

  ares_free_string(exp_server_string);
}

TEST_P(MockChannelTest, ServStateCallbackRecover) {
  // Set up the server response. The server initially times out, but then
  // returns successfully (with NXDOMAIN) on the first retry.
  std::vector<byte> nothing;
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(NXDOMAIN);
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReplyData(&server_, nothing))
    .WillOnce(SetReply(&server_, &rsp));

  // Set up the server state callback. The channel used for this test has a
  // single server configured.
  char *exp_server_string = ares_get_servers_csv(channel_);
  ares_set_server_state_callback(channel_, ServerStateCallback,
                                 &exp_server_string);

  // Perform the hostname lookup. Expect 1 failed query and 1 successful query
  // to the server.
  HostResult result;
  server_state_cb_success_count = 0;
  server_state_cb_failure_count = 0;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback,
                     &result);
  Process();
  EXPECT_EQ(1, server_state_cb_success_count);
  EXPECT_EQ(1, server_state_cb_failure_count);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENOTFOUND, result.status_);

  ares_free_string(exp_server_string);
}

TEST_P(MockChannelTest, ReInit) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  EXPECT_EQ(ARES_SUCCESS, ares_reinit(channel_));
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

#define MAXUDPQUERIES_TOTAL 32
#define MAXUDPQUERIES_LIMIT 8

class MockUDPMaxQueriesTest
    : public MockChannelOptsTest,
      public ::testing::WithParamInterface<int> {
 public:
  MockUDPMaxQueriesTest()
    : MockChannelOptsTest(1, GetParam(), false, false,
                          FillOptions(&opts_),
                          ARES_OPT_UDP_MAX_QUERIES) {}
  static struct ares_options* FillOptions(struct ares_options * opts) {
    memset(opts, 0, sizeof(struct ares_options));
    opts->udp_max_queries = MAXUDPQUERIES_LIMIT;
    return opts;
  }
 private:
  struct ares_options opts_;
};

TEST_P(MockUDPMaxQueriesTest, GetHostByNameParallelLookups) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));

  // Get notified of new sockets so we can validate how many are created
  int rc = ARES_SUCCESS;
  ares_set_socket_callback(channel_, SocketConnectCallback, &rc);
  sock_cb_count = 0;

  HostResult result[MAXUDPQUERIES_TOTAL];
  for (size_t i=0; i<MAXUDPQUERIES_TOTAL; i++) {
    ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result[i]);
  }

  Process();

  EXPECT_EQ(MAXUDPQUERIES_TOTAL / MAXUDPQUERIES_LIMIT, sock_cb_count);

  for (size_t i=0; i<MAXUDPQUERIES_TOTAL; i++) {
    std::stringstream ss;
    EXPECT_TRUE(result[i].done_);
    ss << result[i].host_;
    EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
  }
}

class CacheQueriesTest
    : public MockChannelOptsTest,
      public ::testing::WithParamInterface<int> {
 public:
  CacheQueriesTest()
    : MockChannelOptsTest(1, GetParam(), false, false,
                          FillOptions(&opts_),
                          ARES_OPT_QUERY_CACHE) {}
  static struct ares_options* FillOptions(struct ares_options * opts) {
    memset(opts, 0, sizeof(struct ares_options));
    opts->qcache_max_ttl = 3600;
    return opts;
  }
 private:
  struct ares_options opts_;
};

TEST_P(CacheQueriesTest, GetHostByNameCache) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));

  // Get notified of new sockets so we can validate how many are created
  int rc = ARES_SUCCESS;
  ares_set_socket_callback(channel_, SocketConnectCallback, &rc);
  sock_cb_count = 0;

  HostResult result1;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result1);
  Process();

  std::stringstream ss1;
  EXPECT_TRUE(result1.done_);
  ss1 << result1.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss1.str());

  /* Run again, should return cached result */
  HostResult result2;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result2);
  Process();

  std::stringstream ss2;
  EXPECT_TRUE(result2.done_);
  ss2 << result2.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss2.str());

  EXPECT_EQ(1, sock_cb_count);
}

#define TCPPARALLELLOOKUPS 32
TEST_P(MockTCPChannelTest, GetHostByNameParallelLookups) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));

  // Get notified of new sockets so we can validate how many are created
  int rc = ARES_SUCCESS;
  ares_set_socket_callback(channel_, SocketConnectCallback, &rc);
  sock_cb_count = 0;

  HostResult result[TCPPARALLELLOOKUPS];
  for (size_t i=0; i<TCPPARALLELLOOKUPS; i++) {
    ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result[i]);
  }

  Process();

  EXPECT_EQ(1, sock_cb_count);

  for (size_t i=0; i<TCPPARALLELLOOKUPS; i++) {
    std::stringstream ss;
    EXPECT_TRUE(result[i].done_);
    ss << result[i].host_;
    EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
  }
}

TEST_P(MockTCPChannelTest, MalformedResponse) {
  std::vector<byte> one = {0x00};
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReplyData(&server_, one));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_EBADRESP, result.status_);
}

TEST_P(MockTCPChannelTest, FormErrResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(FORMERR);
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_EFORMERR, result.status_);
}

TEST_P(MockTCPChannelTest, ServFailResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(SERVFAIL);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ESERVFAIL, result.status_);
}

TEST_P(MockTCPChannelTest, NotImplResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(NOTIMP);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENOTIMP, result.status_);
}

TEST_P(MockTCPChannelTest, RefusedResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(REFUSED);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_EREFUSED, result.status_);
}

TEST_P(MockTCPChannelTest, YXDomainResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(YXDOMAIN);
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENODATA, result.status_);
}

class MockExtraOptsTest
    : public MockChannelOptsTest,
      public ::testing::WithParamInterface< std::pair<int, bool> > {
 public:
  MockExtraOptsTest()
    : MockChannelOptsTest(1, GetParam().first, GetParam().second, false,
                          FillOptions(&opts_),
                          ARES_OPT_SOCK_SNDBUF|ARES_OPT_SOCK_RCVBUF) {}
  static struct ares_options* FillOptions(struct ares_options * opts) {
    memset(opts, 0, sizeof(struct ares_options));
    // Set a few options that affect socket communications
    opts->socket_send_buffer_size = 514;
    opts->socket_receive_buffer_size = 514;
    return opts;
  }
 private:
  struct ares_options opts_;
};

TEST_P(MockExtraOptsTest, SimpleQuery) {
  ares_set_local_ip4(channel_, 0x7F000001);
  byte addr6[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  ares_set_local_ip6(channel_, addr6);
  ares_set_local_dev(channel_, "dummy");

  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

class MockFlagsChannelOptsTest
    : public MockChannelOptsTest,
      public ::testing::WithParamInterface< std::pair<int, bool> > {
 public:
  MockFlagsChannelOptsTest(int flags)
    : MockChannelOptsTest(1, GetParam().first, GetParam().second, false,
                          FillOptions(&opts_, flags), ARES_OPT_FLAGS) {}
  static struct ares_options* FillOptions(struct ares_options * opts, int flags) {
    memset(opts, 0, sizeof(struct ares_options));
    opts->flags = flags;
    return opts;
  }
 private:
  struct ares_options opts_;
};

class MockNoCheckRespChannelTest : public MockFlagsChannelOptsTest {
 public:
  MockNoCheckRespChannelTest() : MockFlagsChannelOptsTest(ARES_FLAG_NOCHECKRESP) {}
};

TEST_P(MockNoCheckRespChannelTest, ServFailResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(SERVFAIL);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ESERVFAIL, result.status_);
}

TEST_P(MockNoCheckRespChannelTest, NotImplResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(NOTIMP);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENOTIMP, result.status_);
}

TEST_P(MockNoCheckRespChannelTest, RefusedResponse) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A));
  rsp.set_rcode(REFUSED);
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_EREFUSED, result.status_);
}

class MockEDNSChannelTest : public MockFlagsChannelOptsTest {
 public:
  MockEDNSChannelTest() : MockFlagsChannelOptsTest(ARES_FLAG_EDNS) {}
};

TEST_P(MockEDNSChannelTest, RetryWithoutEDNS) {
  DNSPacket rspfail;
  rspfail.set_response().set_aa().set_rcode(FORMERR)
    .add_question(new DNSQuestion("www.google.com", T_A));
  DNSPacket rspok;
  rspok.set_response()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {1, 2, 3, 4}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rspfail))
    .WillOnce(SetReply(&server_, &rspok));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}


// Issue #911
TEST_P(MockUDPChannelTest, RetryWithoutEDNSNonCompliant) {
  DNSPacket rspfail;
  rspfail.set_response().set_aa().set_rcode(FORMERR)
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, { }, false));
  DNSPacket rspok;
  rspok.set_response()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 100, {1, 2, 3, 4}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &rspfail))
    .WillOnce(SetReply(&server_, &rspok));
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockChannelTest, SearchDomains) {
  DNSPacket nofirst;
  nofirst.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.first.com", T_A));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.second.org", T_A));
  ON_CALL(server_, OnRequest("www.second.org", T_A))
    .WillByDefault(SetReply(&server_, &nosecond));
  DNSPacket yesthird;
  yesthird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A))
    .add_answer(new DNSARR("www.third.gov", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillByDefault(SetReply(&server_, &yesthird));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.third.gov' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

#ifdef HAVE_CONTAINER
// Issue #852
class ContainedMockChannelSysConfig
    : public MockChannelOptsTest,
      public ::testing::WithParamInterface<std::pair<int, bool>> {
 public:
  ContainedMockChannelSysConfig()
    : MockChannelOptsTest(1, GetParam().first, GetParam().second, true, nullptr, 0) {}
};

static NameContentList files_no_ndots = {
  {"/etc/resolv.conf", "nameserver 1.2.3.4\n" // Will be replaced
                       "search example.com example.org\n"
                       "options edns0 trust-ad\n"}, // ndots:1 is default
  {"/etc/hosts", "3.4.5.6 ahostname.com\n"},
  {"/etc/nsswitch.conf", "hosts: files dns\n"}};
CONTAINED_TEST_P(ContainedMockChannelSysConfig, SysConfigNdotsDefault,
                 "myhostname", "mydomainname.org", files_no_ndots) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.example.com", T_A))
    .add_answer(new DNSARR("www.example.com", 0x0200, {2, 3, 4, 5}));
  EXPECT_CALL(server_, OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(&server_, &rsp));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
  return HasFailure();
}


static NameContentList files_ndots0 = {
  {"/etc/resolv.conf", "nameserver 1.2.3.4\n" // Will be replaced
                       "search example.com example.org\n"
                       "options edns0 trust-ad ndots:0\n"}, // ndots:1 is default
  {"/etc/hosts", "3.4.5.6 ahostname.com\n"},
  {"/etc/nsswitch.conf", "hosts: files dns\n"}};
CONTAINED_TEST_P(ContainedMockChannelSysConfig, SysConfigNdots0,
                 "myhostname", "mydomainname.org", files_ndots0) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("www", T_A))
    .add_answer(new DNSARR("www", 0x0200, {1, 2, 3, 4}));
  EXPECT_CALL(server_, OnRequest("www", T_A))
    .WillOnce(SetReply(&server_, &rsp));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www' aliases=[] addrs=[1.2.3.4]}", ss.str());
  return HasFailure();
}
#endif

// Issue #858
TEST_P(CacheQueriesTest, BlankName) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion(".", T_SOA))
    .add_answer(new DNSSoaRR(".", 600, "a.root-servers.net", "nstld.verisign-grs.com", 123456, 3600, 3600, 3600, 3600));
  EXPECT_CALL(server_, OnRequest("", T_SOA))
    .WillOnce(SetReply(&server_, &rsp));

  QueryResult result;
  ares_query_dnsrec(channel_, ".", ARES_CLASS_IN, ARES_REC_TYPE_SOA, QueryCallback, &result, NULL);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(0, result.timeouts_);

  QueryResult cacheresult;
  ares_query_dnsrec(channel_, ".", ARES_CLASS_IN, ARES_REC_TYPE_SOA, QueryCallback, &cacheresult, NULL);
  Process();
  EXPECT_TRUE(cacheresult.done_);
  EXPECT_EQ(0, cacheresult.timeouts_);
}

TEST_P(CacheQueriesTest, SearchDomainsCache) {
  DNSPacket nofirst;
  nofirst.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.first.com", T_A))
    .add_auth(new DNSSoaRR("first.com", 600, "ns1.first.com", "admin.first.com", 123456, 3600, 3600, 3600, 3600));
  EXPECT_CALL(server_, OnRequest("www.first.com", T_A))
    .WillOnce(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.second.org", T_A))
    .add_auth(new DNSSoaRR("second.org", 600, "ns1.second.org", "admin.second.org", 123456, 3600, 3600, 3600, 3600));
  EXPECT_CALL(server_, OnRequest("www.second.org", T_A))
    .WillOnce(SetReply(&server_, &nosecond));
  DNSPacket yesthird;
  yesthird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A))
    .add_answer(new DNSARR("www.third.gov", 0x0200, {2, 3, 4, 5}));
  EXPECT_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillOnce(SetReply(&server_, &yesthird));

  // First pass through should send the queries.  The EXPECT_CALL .WillOnce
  // will make sure this only happens once (vs ON_CALL .WillByDefault)
  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.third.gov' aliases=[] addrs=[2.3.4.5]}", ss.str());

  // This pass should be fully served by cache and yield the same result
  HostResult cacheresult;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &cacheresult);
  Process();
  EXPECT_TRUE(cacheresult.done_);
  std::stringstream sscache;
  sscache << cacheresult.host_;
  EXPECT_EQ("{'www.third.gov' aliases=[] addrs=[2.3.4.5]}", sscache.str());
}

// Relies on retries so is UDP-only
TEST_P(MockUDPChannelTest, SearchDomainsWithResentReply) {
  DNSPacket nofirst;
  nofirst.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.first.com", T_A));
  EXPECT_CALL(server_, OnRequest("www.first.com", T_A))
    .WillOnce(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.second.org", T_A));
  EXPECT_CALL(server_, OnRequest("www.second.org", T_A))
    .WillOnce(SetReply(&server_, &nosecond));
  DNSPacket yesthird;
  yesthird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A))
    .add_answer(new DNSARR("www.third.gov", 0x0200, {2, 3, 4, 5}));
  // Before sending the real answer, resend an earlier reply
  EXPECT_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillOnce(DoAll(SetReply(&server_, &nofirst),
                    SetReplyQID(&server_, 123)))
    .WillOnce(DoAll(SetReply(&server_, &yesthird),
                    SetReplyQID(&server_, -1)));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.third.gov' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, SearchDomainsBare) {
  DNSPacket nofirst;
  nofirst.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.first.com", T_A));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.second.org", T_A));
  ON_CALL(server_, OnRequest("www.second.org", T_A))
    .WillByDefault(SetReply(&server_, &nosecond));
  DNSPacket nothird;
  nothird.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.third.gov", T_A));
  ON_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillByDefault(SetReply(&server_, &nothird));
  DNSPacket yesbare;
  yesbare.set_response().set_aa()
    .add_question(new DNSQuestion("www", T_A))
    .add_answer(new DNSARR("www", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www", T_A))
    .WillByDefault(SetReply(&server_, &yesbare));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(0, result.timeouts_);

  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, SearchNoDataThenSuccess) {
  // First two search domains recognize the name but have no A records.
  DNSPacket nofirst;
  nofirst.set_response().set_aa()
    .add_question(new DNSQuestion("www.first.com", T_A));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa()
    .add_question(new DNSQuestion("www.second.org", T_A));
  ON_CALL(server_, OnRequest("www.second.org", T_A))
    .WillByDefault(SetReply(&server_, &nosecond));
  DNSPacket yesthird;
  yesthird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A))
    .add_answer(new DNSARR("www.third.gov", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillByDefault(SetReply(&server_, &yesthird));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.third.gov' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, SearchNoDataThenNoDataBare) {
  // First two search domains recognize the name but have no A records.
  DNSPacket nofirst;
  nofirst.set_response().set_aa()
    .add_question(new DNSQuestion("www.first.com", T_A));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa()
    .add_question(new DNSQuestion("www.second.org", T_A));
  ON_CALL(server_, OnRequest("www.second.org", T_A))
    .WillByDefault(SetReply(&server_, &nosecond));
  DNSPacket nothird;
  nothird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A));
  ON_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillByDefault(SetReply(&server_, &nothird));
  DNSPacket nobare;
  nobare.set_response().set_aa()
    .add_question(new DNSQuestion("www", T_A));
  ON_CALL(server_, OnRequest("www", T_A))
    .WillByDefault(SetReply(&server_, &nobare));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENODATA, result.status_);
}

TEST_P(MockChannelTest, SearchNoDataThenFail) {
  // First two search domains recognize the name but have no A records.
  DNSPacket nofirst;
  nofirst.set_response().set_aa()
    .add_question(new DNSQuestion("www.first.com", T_A));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa()
    .add_question(new DNSQuestion("www.second.org", T_A));
  ON_CALL(server_, OnRequest("www.second.org", T_A))
    .WillByDefault(SetReply(&server_, &nosecond));
  DNSPacket nothird;
  nothird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A));
  ON_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillByDefault(SetReply(&server_, &nothird));
  DNSPacket nobare;
  nobare.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www", T_A));
  ON_CALL(server_, OnRequest("www", T_A))
    .WillByDefault(SetReply(&server_, &nobare));

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENODATA, result.status_);
}

TEST_P(MockChannelTest, SearchAllocFailure) {
  SearchResult result;
  SetAllocFail(1);
  ares_search(channel_, "fully.qualified.", C_IN, T_A, SearchCallback, &result);
  /* Already done */
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENOMEM, result.status_);
}

TEST_P(MockChannelTest, SearchHighNdots) {
  DNSPacket nobare;
  nobare.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("a.b.c.w.w.w", T_A));
  ON_CALL(server_, OnRequest("a.b.c.w.w.w", T_A))
    .WillByDefault(SetReply(&server_, &nobare));
  DNSPacket yesfirst;
  yesfirst.set_response().set_aa()
    .add_question(new DNSQuestion("a.b.c.w.w.w.first.com", T_A))
    .add_answer(new DNSARR("a.b.c.w.w.w.first.com", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("a.b.c.w.w.w.first.com", T_A))
    .WillByDefault(SetReply(&server_, &yesfirst));

  SearchResult result;
  ares_search(channel_, "a.b.c.w.w.w", C_IN, T_A, SearchCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_SUCCESS, result.status_);
  std::stringstream ss;
  ss << PacketToString(result.data_);
  EXPECT_EQ("RSP QRY AA NOERROR Q:{'a.b.c.w.w.w.first.com' IN A} "
            "A:{'a.b.c.w.w.w.first.com' IN A TTL=512 2.3.4.5}",
            ss.str());
}

// Test that performing an EDNS search with an OPT RR options value works. The
// options value should be included on the requests to the mock server.
// We are going to do this only via TCP since this won't include the dynamically
// generated DNS cookie that would otherwise mess with this result.
TEST_P(MockTCPChannelTest, SearchOptVal) {
  /* Define the OPT RR options code and value to use. */
  unsigned short opt_opt = 3;
  unsigned char opt_val[] = { 'c', '-', 'a', 'r', 'e', 's' };

  /* Set up the expected request and reply on the mock server for the first,
   * second and third domains. The expected requests contain the OPT RR options
   * value defined above.
   */
  std::string nofirst_req = "REQ QRY RD  Q:{'example.first.com' IN A} "
    "ADD:{'' MAXUDP=1232 OPT RCODE2=0 "
    "0003"  // opt_opt
    "0006"  // length of opt_val
    "632d61726573"  // opt_val in hex
    "}";
  DNSPacket nofirst_rep;
  nofirst_rep.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("example.first.com", T_A));
  ON_CALL(server_, OnRequest("example.first.com", T_A))
    .WillByDefault(SetReplyExpRequest(&server_, &nofirst_rep, nofirst_req));

  std::string nosecond_req = "REQ QRY RD  Q:{'example.second.org' IN A} "
    "ADD:{'' MAXUDP=1232 OPT RCODE2=0 "
    "0003"  // opt_opt
    "0006"  // length of opt_val
    "632d61726573"  // opt_val in hex
    "}";
  DNSPacket nosecond_rep;
  nosecond_rep.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("example.second.org", T_A));
  ON_CALL(server_, OnRequest("example.second.org", T_A))
    .WillByDefault(SetReplyExpRequest(&server_, &nosecond_rep, nosecond_req));

  std::string nothird_req = "REQ QRY RD  Q:{'example.third.gov' IN A} "
    "ADD:{'' MAXUDP=1232 OPT RCODE2=0 "
    "0003"  // opt_opt
    "0006"  // length of opt_val
    "632d61726573"  // opt_val in hex
    "}";
  DNSPacket nothird_rep;
  nothird_rep.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("example.third.gov", T_A));
  ON_CALL(server_, OnRequest("example.third.gov", T_A))
    .WillByDefault(SetReplyExpRequest(&server_, &nothird_rep, nothird_req));

  /* Set up the expected request and reply on the mock server for the bare
   * domain. The expected request contains the OPT RR options value defined
   * above.
   */
  std::string yesbare_req = "REQ QRY RD  Q:{'example' IN A} "
    "ADD:{'' MAXUDP=1232 OPT RCODE2=0 "
    "0003"  // opt_opt
    "0006"  // length of opt_val
    "632d61726573"  // opt_val in hex
    "}";
  DNSPacket yesbare_rep;
  yesbare_rep.set_response().set_aa()
    .add_question(new DNSQuestion("example", T_A))
    .add_answer(new DNSARR("example", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("example", T_A))
    .WillByDefault(SetReplyExpRequest(&server_, &yesbare_rep, yesbare_req));

  /* Construct the DNS record to search. */
  ares_dns_record_t *dnsrec = NULL;
  ares_dns_rr_t *rr = NULL;
  EXPECT_EQ(ARES_SUCCESS,
    ares_dns_record_create(&dnsrec, 0, ARES_FLAG_RD, ARES_OPCODE_QUERY,
      ARES_RCODE_NOERROR));
  EXPECT_EQ(ARES_SUCCESS,
    ares_dns_record_query_add(dnsrec, "example", (ares_dns_rec_type_t)T_A,
      (ares_dns_class_t)C_IN));
  EXPECT_EQ(ARES_SUCCESS,
    ares_dns_record_rr_add(&rr, dnsrec, ARES_SECTION_ADDITIONAL, "",
      ARES_REC_TYPE_OPT, ARES_CLASS_IN, 0));
  EXPECT_EQ(ARES_SUCCESS,
    ares_dns_rr_set_u16(rr, ARES_RR_OPT_UDP_SIZE, 1232));
  EXPECT_EQ(ARES_SUCCESS, ares_dns_rr_set_u8(rr, ARES_RR_OPT_VERSION, 0));
  EXPECT_EQ(ARES_SUCCESS, ares_dns_rr_set_u16(rr, ARES_RR_OPT_FLAGS, 0));
  EXPECT_EQ(ARES_SUCCESS,
    ares_dns_rr_set_opt(rr, ARES_RR_OPT_OPTIONS, opt_opt, opt_val,
      sizeof(opt_val)));

  /* Perform the search. Check that it succeeds with the expected response. */
  SearchResult result;
  ares_search_dnsrec(channel_, dnsrec, SearchCallbackDnsRec, &result);
  ares_dns_record_destroy(dnsrec);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_SUCCESS, result.status_);
  std::stringstream ss;
  ss << PacketToString(result.data_);
  EXPECT_EQ("RSP QRY AA NOERROR Q:{'example' IN A} "
            "A:{'example' IN A TTL=512 2.3.4.5}",
            ss.str());
}

TEST_P(MockChannelTest, V4WorksV6Timeout) {
  std::vector<byte> nothing;
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));

  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  ON_CALL(server_, OnRequest("www.google.com", T_AAAA))
    .WillByDefault(SetReplyData(&server_, nothing));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_UNSPEC, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(1, result.timeouts_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

// Test case for Issue #662
TEST_P(MockChannelTest, PartialQueryCancel) {
  std::vector<byte> nothing;
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));

  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  ON_CALL(server_, OnRequest("www.google.com", T_AAAA))
    .WillByDefault(SetReplyData(&server_, nothing));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_UNSPEC, HostCallback, &result);
  // After 100ms, issues ares_cancel(), this should be enough time for the A
  // record reply, but before the timeout on the AAAA record.
  Process(100);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ECANCELLED, result.status_);
}

TEST_P(MockChannelTest, UnspecifiedFamilyV6) {
  DNSPacket rsp6;
  rsp6.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_AAAA))
    .add_answer(new DNSAaaaRR("example.com", 100,
                              {0x21, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03}));
  ON_CALL(server_, OnRequest("example.com", T_AAAA))
    .WillByDefault(SetReply(&server_, &rsp6));

  DNSPacket rsp4;
  rsp4.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_A));
  ON_CALL(server_, OnRequest("example.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp4));

  HostResult result;
  ares_gethostbyname(channel_, "example.com.", AF_UNSPEC, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  // Default to IPv6 when both are available.
  EXPECT_EQ("{'example.com' aliases=[] addrs=[2121:0000:0000:0000:0000:0000:0000:0303]}", ss.str());
}

TEST_P(MockChannelTest, UnspecifiedFamilyV4) {
  DNSPacket rsp6;
  rsp6.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_AAAA));
  ON_CALL(server_, OnRequest("example.com", T_AAAA))
    .WillByDefault(SetReply(&server_, &rsp6));
  DNSPacket rsp4;
  rsp4.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_A))
    .add_answer(new DNSARR("example.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("example.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp4));

  HostResult result;
  ares_gethostbyname(channel_, "example.com.", AF_UNSPEC, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'example.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, UnspecifiedFamilyNoData) {
  DNSPacket rsp6;
  rsp6.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_AAAA))
    .add_answer(new DNSCnameRR("example.com", 100, "elsewhere.com"));
  ON_CALL(server_, OnRequest("example.com", T_AAAA))
    .WillByDefault(SetReply(&server_, &rsp6));
  DNSPacket rsp4;
  rsp4.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_A));
  ON_CALL(server_, OnRequest("example.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp4));

  HostResult result;
  ares_gethostbyname(channel_, "example.com.", AF_UNSPEC, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'' aliases=[] addrs=[]}", ss.str());
}

TEST_P(MockChannelTest, UnspecifiedFamilyCname6A4) {
  DNSPacket rsp6;
  rsp6.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_AAAA))
    .add_answer(new DNSCnameRR("example.com", 100, "elsewhere.com"));
  ON_CALL(server_, OnRequest("example.com", T_AAAA))
    .WillByDefault(SetReply(&server_, &rsp6));
  DNSPacket rsp4;
  rsp4.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_A))
    .add_answer(new DNSARR("example.com", 100, {1, 2, 3, 4}));
  ON_CALL(server_, OnRequest("example.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp4));

  HostResult result;
  ares_gethostbyname(channel_, "example.com.", AF_UNSPEC, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'example.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockChannelTest, ExplicitIP) {
  HostResult result;
  ares_gethostbyname(channel_, "1.2.3.4", AF_INET, HostCallback, &result);
  EXPECT_TRUE(result.done_);  // Immediate return
  EXPECT_EQ(ARES_SUCCESS, result.status_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'1.2.3.4' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockChannelTest, ExplicitIPAllocFail) {
  HostResult result;
  SetAllocSizeFail(strlen("1.2.3.4") + 1);
  ares_gethostbyname(channel_, "1.2.3.4", AF_INET, HostCallback, &result);
  EXPECT_TRUE(result.done_);  // Immediate return
  EXPECT_EQ(ARES_ENOMEM, result.status_);
}

TEST_P(MockChannelTest, SortListV4) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_A))
    .add_answer(new DNSARR("example.com", 100, {22, 23, 24, 25}))
    .add_answer(new DNSARR("example.com", 100, {12, 13, 14, 15}))
    .add_answer(new DNSARR("example.com", 100, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("example.com", T_A))
    .WillByDefault(SetReply(&server_, &rsp));

  {
    EXPECT_EQ(ARES_SUCCESS, ares_set_sortlist(channel_, "12.13.0.0/255.255.0.0 1234::5678"));
    HostResult result;
    ares_gethostbyname(channel_, "example.com.", AF_INET, HostCallback, &result);
    Process();
    EXPECT_TRUE(result.done_);
    std::stringstream ss;
    ss << result.host_;
    EXPECT_EQ("{'example.com' aliases=[] addrs=[12.13.14.15, 22.23.24.25, 2.3.4.5]}", ss.str());
  }
  {
    EXPECT_EQ(ARES_SUCCESS, ares_set_sortlist(channel_, "2.3.0.0/16 130.140.150.160/26"));
    HostResult result;
    ares_gethostbyname(channel_, "example.com.", AF_INET, HostCallback, &result);
    Process();
    EXPECT_TRUE(result.done_);
    std::stringstream ss;
    ss << result.host_;
    EXPECT_EQ("{'example.com' aliases=[] addrs=[2.3.4.5, 22.23.24.25, 12.13.14.15]}", ss.str());
  }
  struct ares_options options;
  memset(&options, 0, sizeof(options));
  int optmask = 0;
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(channel_, &options, &optmask));
  EXPECT_TRUE((optmask & ARES_OPT_SORTLIST) == ARES_OPT_SORTLIST);
  ares_destroy_options(&options);
}

TEST_P(MockChannelTest, SortListV6) {
  DNSPacket rsp;
  rsp.set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_AAAA))
    .add_answer(new DNSAaaaRR("example.com", 100,
                              {0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02}))
    .add_answer(new DNSAaaaRR("example.com", 100,
                              {0x21, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03}));
  ON_CALL(server_, OnRequest("example.com", T_AAAA))
    .WillByDefault(SetReply(&server_, &rsp));

  {
    ares_set_sortlist(channel_, "1111::/16 2.3.0.0/255.255.0.0");
    HostResult result;
    ares_gethostbyname(channel_, "example.com.", AF_INET6, HostCallback, &result);
    Process();
    EXPECT_TRUE(result.done_);
    std::stringstream ss;
    ss << result.host_;
    EXPECT_EQ("{'example.com' aliases=[] addrs=[1111:0000:0000:0000:0000:0000:0000:0202, "
              "2121:0000:0000:0000:0000:0000:0000:0303]}", ss.str());
  }
  {
    ares_set_sortlist(channel_, "2121::/8");
    HostResult result;
    ares_gethostbyname(channel_, "example.com.", AF_INET6, HostCallback, &result);
    Process();
    EXPECT_TRUE(result.done_);
    std::stringstream ss;
    ss << result.host_;
    EXPECT_EQ("{'example.com' aliases=[] addrs=[2121:0000:0000:0000:0000:0000:0000:0303, "
              "1111:0000:0000:0000:0000:0000:0000:0202]}", ss.str());
  }
}

// Relies on retries so is UDP-only
TEST_P(MockUDPChannelTest, SearchDomainsAllocFail) {
  DNSPacket nofirst;
  nofirst.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.first.com", T_A));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &nofirst));
  DNSPacket nosecond;
  nosecond.set_response().set_aa().set_rcode(NXDOMAIN)
    .add_question(new DNSQuestion("www.second.org", T_A));
  ON_CALL(server_, OnRequest("www.second.org", T_A))
    .WillByDefault(SetReply(&server_, &nosecond));
  DNSPacket yesthird;
  yesthird.set_response().set_aa()
    .add_question(new DNSQuestion("www.third.gov", T_A))
    .add_answer(new DNSARR("www.third.gov", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.third.gov", T_A))
    .WillByDefault(SetReply(&server_, &yesthird));

  // Fail a variety of different memory allocations, and confirm
  // that the operation either fails with ENOMEM or succeeds
  // with the expected result.
  const int kCount = 34;
  HostResult results[kCount];
  for (int ii = 1; ii <= kCount; ii++) {
    HostResult* result = &(results[ii - 1]);
    ClearFails();
    SetAllocFail(ii);
    ares_gethostbyname(channel_, "www", AF_INET, HostCallback, result);
    Process();
    EXPECT_TRUE(result->done_);
    if (result->status_ == ARES_SUCCESS) {
      std::stringstream ss;
      ss << result->host_;
      EXPECT_EQ("{'www.third.gov' aliases=[] addrs=[2.3.4.5]}", ss.str()) << " failed alloc #" << ii;
      if (verbose) std::cerr << "Succeeded despite failure of alloc #" << ii << std::endl;
    }
  }

  // Explicitly destroy the channel now, so that the HostResult objects
  // are still valid (in case any pending work refers to them).
  ares_destroy(channel_);
  channel_ = nullptr;
}

// Relies on retries so is UDP-only
TEST_P(MockUDPChannelTest, Resend) {
  std::vector<byte> nothing;
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));

  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReplyData(&server_, nothing))
    .WillOnce(SetReplyData(&server_, nothing))
    .WillOnce(SetReply(&server_, &reply));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(2, result.timeouts_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockChannelTest, CancelImmediate) {
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  ares_cancel(channel_);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ECANCELLED, result.status_);
  EXPECT_EQ(0, result.timeouts_);
}

TEST_P(MockChannelTest, CancelImmediateGetHostByAddr) {
  HostResult result;
  struct in_addr addr;
  addr.s_addr = htonl(0x08080808);

  ares_gethostbyaddr(channel_, &addr, sizeof(addr), AF_INET, HostCallback, &result);
  ares_cancel(channel_);
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ECANCELLED, result.status_);
  EXPECT_EQ(0, result.timeouts_);
}

// Relies on retries so is UDP-only
TEST_P(MockUDPChannelTest, CancelLater) {
  std::vector<byte> nothing;

  // On second request, cancel the channel.
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReplyData(&server_, nothing))
    .WillOnce(CancelChannel(&server_, channel_));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ECANCELLED, result.status_);
  EXPECT_EQ(0, result.timeouts_);
}

TEST_P(MockChannelTest, DisconnectFirstAttempt) {
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));

  // On second request, cancel the channel.
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(Disconnect(&server_))
    .WillOnce(SetReply(&server_, &reply));

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockChannelTest, GetHostByNameDestroyAbsolute) {
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);

  ares_destroy(channel_);
  channel_ = nullptr;

  EXPECT_TRUE(result.done_);  // Synchronous
  EXPECT_EQ(ARES_EDESTRUCTION, result.status_);
  EXPECT_EQ(0, result.timeouts_);
}

TEST_P(MockChannelTest, GetHostByNameDestroyRelative) {
  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);

  ares_destroy(channel_);
  channel_ = nullptr;

  EXPECT_TRUE(result.done_);  // Synchronous
  EXPECT_EQ(ARES_EDESTRUCTION, result.status_);
  EXPECT_EQ(0, result.timeouts_);
}

TEST_P(MockChannelTest, GetHostByNameCNAMENoData) {
  DNSPacket response;
  response.set_response().set_aa()
    .add_question(new DNSQuestion("cname.first.com", T_A))
    .add_answer(new DNSCnameRR("cname.first.com", 100, "a.first.com"));
  ON_CALL(server_, OnRequest("cname.first.com", T_A))
    .WillByDefault(SetReply(&server_, &response));

  HostResult result;
  ares_gethostbyname(channel_, "cname.first.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_ENODATA, result.status_);
}

TEST_P(MockChannelTest, GetHostByAddrDestroy) {
  unsigned char gdns_addr4[4] = {0x08, 0x08, 0x08, 0x08};
  HostResult result;
  ares_gethostbyaddr(channel_, gdns_addr4, sizeof(gdns_addr4), AF_INET, HostCallback, &result);

  ares_destroy(channel_);
  channel_ = nullptr;

  EXPECT_TRUE(result.done_);  // Synchronous
  EXPECT_EQ(ARES_EDESTRUCTION, result.status_);
  EXPECT_EQ(0, result.timeouts_);
}

TEST_P(MockChannelTest, TriggerResendThenConnFailSERVFAIL) {
  // Set up the server response. The server always returns SERVFAIL.
  DNSPacket badrsp;
  badrsp.set_response().set_aa().set_rcode(SERVFAIL)
    .add_question(new DNSQuestion("www.google.com", T_A));
  DNSPacket goodrsp;
  goodrsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReplyAndFailSend(&server_, &badrsp))
    .WillOnce(SetReply(&server_, &goodrsp));

  ares_socket_functions sock_funcs;
  memset(&sock_funcs, 0, sizeof(sock_funcs));

  sock_funcs.asendv = ares_sendv_fail;

  ares_set_socket_functions(channel_, &sock_funcs, NULL);

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback,
                     &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockUDPChannelTest, TriggerResendThenConnFailEDNS) {
  // Set up the server response to simulate an EDNS failure
  DNSPacket badrsp;
  badrsp.set_response().set_aa().set_rcode(FORMERR)
    .add_question(new DNSQuestion("www.google.com", T_A));
  DNSPacket goodrsp;
  goodrsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReplyAndFailSend(&server_, &badrsp))
    .WillOnce(SetReply(&server_, &goodrsp));

  ares_socket_functions sock_funcs;
  memset(&sock_funcs, 0, sizeof(sock_funcs));

  sock_funcs.asendv = ares_sendv_fail;

  ares_set_socket_functions(channel_, &sock_funcs, NULL);

  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback,
                     &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockUDPChannelTest, GetSock) {
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  ares_socket_t socks[3] = {ARES_SOCKET_BAD, ARES_SOCKET_BAD, ARES_SOCKET_BAD};
  int bitmask;

  bitmask = ares_getsock(channel_, socks, 3);
  EXPECT_EQ(0, bitmask);
  bitmask = ares_getsock(channel_, nullptr, 0);
  EXPECT_EQ(0, bitmask);

  // Ask again with a pending query.
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  bitmask = ares_getsock(channel_, socks, 3);
  EXPECT_NE(0, bitmask);

  size_t sock_cnt = 0;
  for (size_t i=0; i<3; i++) {
    if (ARES_GETSOCK_READABLE(bitmask, i) || ARES_GETSOCK_WRITABLE(bitmask, i)) {
      EXPECT_NE(ARES_SOCKET_BAD, socks[i]);
      if (socks[i] != ARES_SOCKET_BAD)
        sock_cnt++;
    }
  }
  EXPECT_NE((size_t)0, sock_cnt);

  Process();

  bitmask = ares_getsock(channel_, nullptr, 0);
  EXPECT_EQ(0, bitmask);
}

TEST_P(MockTCPChannelTest, GetSock) {
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  ares_socket_t socks[3] = {ARES_SOCKET_BAD, ARES_SOCKET_BAD, ARES_SOCKET_BAD};
  int bitmask;

  bitmask = ares_getsock(channel_, socks, 3);
  EXPECT_EQ(0, bitmask);
  bitmask = ares_getsock(channel_, nullptr, 0);
  EXPECT_EQ(0, bitmask);

  // Ask again with a pending query.
  HostResult result;
  ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
  bitmask = ares_getsock(channel_, socks, 3);
  EXPECT_NE(0, bitmask);

  size_t sock_cnt = 0;
  for (size_t i=0; i<3; i++) {
    if (ARES_GETSOCK_READABLE(bitmask, i) || ARES_GETSOCK_WRITABLE(bitmask, i)) {
      EXPECT_NE(ARES_SOCKET_BAD, socks[i]);
      if (socks[i] != ARES_SOCKET_BAD)
        sock_cnt++;
    }
  }
  EXPECT_NE((size_t)0, sock_cnt);

  Process();

  bitmask = ares_getsock(channel_, nullptr, 0);
  EXPECT_EQ(0, bitmask);
}


TEST_P(MockChannelTest, VerifySocketFunctionCallback) {
  ares_socket_functions sock_funcs;
  memset(&sock_funcs, 0, sizeof(sock_funcs));

  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  size_t count = 0;

  sock_funcs.asocket = [](int af, int type, int protocol, void * p) -> ares_socket_t {
    EXPECT_NE(nullptr, p);
    (*reinterpret_cast<size_t *>(p))++;
    return ::socket(af, type, protocol);
  };

  ares_set_socket_functions(channel_, &sock_funcs, &count);

  {
    count = 0;
    HostResult result;
    ares_gethostbyname(channel_, "www.google.com.", AF_INET, HostCallback, &result);
    Process();

    EXPECT_TRUE(result.done_);
    EXPECT_EQ(ARES_SUCCESS, result.status_);
    EXPECT_EQ(0, result.timeouts_);
    EXPECT_NE((size_t)0, count);
  }

  {
    count = 0;
    ares_channel_t *copy;
    EXPECT_EQ(ARES_SUCCESS, ares_dup(&copy, channel_));

    HostResult result;
    ares_gethostbyname(copy, "www.google.com.", AF_INET, HostCallback, &result);

    ProcessAltChannel(copy);

    EXPECT_TRUE(result.done_);
    ares_destroy(copy);
    EXPECT_NE((size_t)0, count);
    EXPECT_EQ(ARES_SUCCESS, result.status_);
    EXPECT_EQ(0, result.timeouts_);
  }

}

static const unsigned char *
  fetch_server_cookie(const ares_dns_record_t *dnsrec, size_t *len)
{
  const ares_dns_rr_t *rr  = fetch_rr_opt(dnsrec);
  const unsigned char *val = NULL;
  *len                     = 0;

  if (rr == NULL) {
    return NULL;
  }

  if (!ares_dns_rr_get_opt_byid(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE,
                                &val, len)) {
    return NULL;
  }

  if (*len <= 8) {
    *len = 0;
    return NULL;
  }

  *len -= 8;
  val  += 8;
  return val;
}

static const unsigned char *
  fetch_client_cookie(const ares_dns_record_t *dnsrec, size_t *len)
{
  const ares_dns_rr_t *rr  = fetch_rr_opt(dnsrec);
  const unsigned char *val = NULL;
  *len                     = 0;

  if (rr == NULL) {
    return NULL;
  }

  if (!ares_dns_rr_get_opt_byid(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE,
                                &val, len)) {
    return NULL;
  }

  if (*len < 8) {
    *len = 0;
    return NULL;
  }

  *len = 8;
  return val;
}

TEST_P(MockUDPChannelTest, DNSCookieSingle) {
  DNSPacket reply;
  std::vector<byte> server_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie, false));
  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &reply));

  QueryResult result;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result, NULL);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(0, result.timeouts_);

  size_t len;
  const unsigned char *returned_cookie = fetch_server_cookie(result.dnsrec_.dnsrec_, &len);
  EXPECT_EQ(len, server_cookie.size());
  EXPECT_TRUE(memcmp(server_cookie.data(), returned_cookie, len) == 0);
}

TEST_P(MockUDPChannelTest, DNSCookieMissingAfterGood) {
  std::vector<byte> server_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie, false));
  DNSPacket reply_nocookie;
  reply_nocookie.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, { }, false));
  DNSPacket reply_ensurecookie;
  reply_ensurecookie.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie, true));

  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &reply))
    .WillOnce(SetReply(&server_, &reply_nocookie))
    .WillOnce(SetReply(&server_, &reply_ensurecookie));

  /* This test will establish the server supports cookies, then the next reply
   * will be missing the server cookie and therefore be rejected and timeout, then
   * an internal retry will occur and the cookie will be present again and it
   * will be verified a server cookie was actually present that matches the
   * server cookie. */
  QueryResult result1;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result1, NULL);
  Process();
  EXPECT_TRUE(result1.done_);
  EXPECT_EQ(0, result1.timeouts_);

  QueryResult result2;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result2, NULL);
  Process();
  EXPECT_TRUE(result2.done_);
  EXPECT_EQ(1, result2.timeouts_);

  /* Client cookie should NOT have rotated */
  size_t len1;
  const unsigned char *client_cookie_1 = fetch_client_cookie(result1.dnsrec_.dnsrec_, &len1);
  size_t len2;
  const unsigned char *client_cookie_2 = fetch_client_cookie(result2.dnsrec_.dnsrec_, &len2);
  EXPECT_EQ(len1, 8);
  EXPECT_EQ(len1, len2);
  EXPECT_TRUE(memcmp(client_cookie_1, client_cookie_2, len1) == 0);
}


TEST_P(MockUDPChannelTest, DNSCookieBadLen) {
  std::vector<byte> server_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };
  std::vector<byte> server_cookie_bad = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0 };
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie, false));
  DNSPacket reply_badcookielen;
  reply_badcookielen.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie_bad, false));

  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &reply_badcookielen))
    .WillOnce(SetReply(&server_, &reply));

  /* This test will send back a malformed cookie len, then when it times out and retry occurs will send back a valid cookie. */
  QueryResult result1;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result1, NULL);
  Process();
  EXPECT_TRUE(result1.done_);
  EXPECT_EQ(1, result1.timeouts_);
}


TEST_P(MockUDPChannelTest, DNSCookieServerRotate) {
  std::vector<byte> server_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };
  std::vector<byte> server_cookie_rotate = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF };

  DNSPacket reply_cookie1;
  reply_cookie1.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, {}, server_cookie, false));
  DNSPacket reply_cookie2_badcookie;
  reply_cookie2_badcookie.set_response().set_aa().set_rcode(ARES_RCODE_BADCOOKIE & 0xF)
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR((ARES_RCODE_BADCOOKIE >> 4) & 0xFF, 0, 0, 1280, { }, server_cookie_rotate, false));
  DNSPacket reply_cookie2;
  reply_cookie2.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie_rotate, true));

  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &reply_cookie1))
    .WillOnce(SetReply(&server_, &reply_cookie2_badcookie))
    .WillOnce(SetReply(&server_, &reply_cookie2));

  /* This test will establish the server supports cookies, then the next reply
   * the server returns BADCOOKIE indicating the cookie has rotated and
   * returns a new cookie. Then the query will be automatically retried with
   * the newly returned cookie. No timeouts should be indicated, and the
   * client cookie should not rotate. */
  QueryResult result1;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result1, NULL);
  Process();
  EXPECT_TRUE(result1.done_);
  EXPECT_EQ(0, result1.timeouts_);

  QueryResult result2;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result2, NULL);
  Process();
  EXPECT_TRUE(result2.done_);
  EXPECT_EQ(0, result2.timeouts_);

  /* Client cookie should NOT have rotated */
  size_t len1;
  const unsigned char *client_cookie_1 = fetch_client_cookie(result1.dnsrec_.dnsrec_, &len1);
  size_t len2;
  const unsigned char *client_cookie_2 = fetch_client_cookie(result2.dnsrec_.dnsrec_, &len2);
  EXPECT_EQ(len1, 8);
  EXPECT_EQ(len1, len2);
  EXPECT_TRUE(memcmp(client_cookie_1, client_cookie_2, len1) == 0);
}

TEST_P(MockUDPChannelTest, DNSCookieSpoof) {
  std::vector<byte> client_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };
  std::vector<byte> server_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };

  DNSPacket reply_spoof;
  reply_spoof.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, client_cookie, server_cookie, false));
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, server_cookie, false));

  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &reply_spoof))
    .WillOnce(SetReply(&server_, &reply));

  /* This test will return a reply that doesn't have the same client cookie as
   * was sent, this should result in a drop of the packet alltogether, then
   * the library will retry and a proper result will be sent. */
  QueryResult result;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result, NULL);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(1, result.timeouts_);
}

TEST_P(MockUDPChannelTest, DNSCookieTCPUpgrade) {
  std::vector<byte> server_cookie = { 1, 2, 3, 4, 5, 6, 7, 8 };

  DNSPacket reply_badcookie;
  reply_badcookie.set_response().set_aa().set_rcode(ARES_RCODE_BADCOOKIE & 0xF)
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR((ARES_RCODE_BADCOOKIE >> 4) & 0xFF, 0, 0, 1280, { }, server_cookie, false));
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}))
    .add_additional(new DNSOptRR(0, 0, 0, 1280, { }, { }, false));

  EXPECT_CALL(server_, OnRequest("www.google.com", T_A))
    .WillOnce(SetReply(&server_, &reply_badcookie))
    .WillOnce(SetReply(&server_, &reply_badcookie))
    .WillOnce(SetReply(&server_, &reply_badcookie))
    .WillOnce(SetReply(&server_, &reply));

  /* This test will establish the server supports cookies, but continuously
   * returns BADCOOKIE which is an indicator that there is some form of
   * AnyCast issue across servers, so it upgrades to TCP afterwards.  No
   * timeouts are recorded as the queries are sent back-to-back as immediate
   * reattempts after the response. */
  QueryResult result;
  ares_query_dnsrec(channel_, "www.google.com", ARES_CLASS_IN, ARES_REC_TYPE_A, QueryCallback, &result, NULL);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(0, result.timeouts_);
}


#ifndef WIN32
TEST_P(MockChannelTest, HostAlias) {
  DNSPacket reply;
  reply.set_response().set_aa()
    .add_question(new DNSQuestion("www.google.com", T_A))
    .add_answer(new DNSARR("www.google.com", 0x0100, {0x01, 0x02, 0x03, 0x04}));
  ON_CALL(server_, OnRequest("www.google.com", T_A))
    .WillByDefault(SetReply(&server_, &reply));

  TempFile aliases("\n\n# www commentedout\nwww www.google.com\n");
  EnvValue with_env("HOSTALIASES", aliases.filename());

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.google.com' aliases=[] addrs=[1.2.3.4]}", ss.str());
}

TEST_P(MockChannelTest, HostAliasMissing) {
  DNSPacket yesfirst;
  yesfirst.set_response().set_aa()
    .add_question(new DNSQuestion("www.first.com", T_A))
    .add_answer(new DNSARR("www.first.com", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &yesfirst));

  TempFile aliases("\n\n# www commentedout\nww www.google.com\n");
  EnvValue with_env("HOSTALIASES", aliases.filename());
  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.first.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, HostAliasMissingFile) {
  DNSPacket yesfirst;
  yesfirst.set_response().set_aa()
    .add_question(new DNSQuestion("www.first.com", T_A))
    .add_answer(new DNSARR("www.first.com", 0x0200, {2, 3, 4, 5}));
  ON_CALL(server_, OnRequest("www.first.com", T_A))
    .WillByDefault(SetReply(&server_, &yesfirst));

  EnvValue with_env("HOSTALIASES", "bogus.mcfile");
  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  std::stringstream ss;
  ss << result.host_;
  EXPECT_EQ("{'www.first.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
}

TEST_P(MockChannelTest, HostAliasUnreadable) {
  TempFile aliases("www www.google.com\n");
  EXPECT_EQ(chmod(aliases.filename(), 0), 0);

  /* Perform OS sanity checks.  We are observing on Debian after the chmod(fn, 0)
   * that we are still able to fopen() the file which is unexpected.  Skip the
   * test if we observe this behavior */
  struct stat st;
  EXPECT_EQ(stat(aliases.filename(), &st), 0);
  EXPECT_EQ(st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO), 0);
  FILE *fp = fopen(aliases.filename(), "r");
  if (fp != NULL) {
    if (verbose) std::cerr << "Skipping Test due to OS incompatibility (open file caching)" << std::endl;
    fclose(fp);
    return;
  }

  EnvValue with_env("HOSTALIASES", aliases.filename());

  HostResult result;
  ares_gethostbyname(channel_, "www", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(ARES_EFILE, result.status_);
  chmod(aliases.filename(), 0777);
}
#endif

class MockMultiServerChannelTest
  : public MockChannelOptsTest,
    public ::testing::WithParamInterface< std::pair<int, bool> > {
 public:
  MockMultiServerChannelTest(ares_options *opts, int optmask)
    : MockChannelOptsTest(3, GetParam().first, GetParam().second, false, opts, optmask) {}
  void CheckExample() {
    HostResult result;
    ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result);
    Process();
    EXPECT_TRUE(result.done_);
    std::stringstream ss;
    ss << result.host_;
    EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
  }
};

class NoRotateMultiMockTest : public MockMultiServerChannelTest {
 public:
  NoRotateMultiMockTest() : MockMultiServerChannelTest(nullptr, ARES_OPT_NOROTATE) {}
};


TEST_P(NoRotateMultiMockTest, ThirdServer) {
  struct ares_options opts;
  int optmask = 0;
  memset(&opts, 0, sizeof(opts));
  EXPECT_EQ(ARES_SUCCESS, ares_save_options(channel_, &opts, &optmask));
  EXPECT_EQ(ARES_OPT_NOROTATE, (optmask & ARES_OPT_NOROTATE));
  ares_destroy_options(&opts);

  DNSPacket servfailrsp;
  servfailrsp.set_response().set_aa().set_rcode(SERVFAIL)
    .add_question(new DNSQuestion("www.example.com", T_A));
  DNSPacket notimplrsp;
  notimplrsp.set_response().set_aa().set_rcode(NOTIMP)
    .add_question(new DNSQuestion("www.example.com", T_A));
  DNSPacket okrsp;
  okrsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.example.com", T_A))
    .add_answer(new DNSARR("www.example.com", 100, {2,3,4,5}));

  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &servfailrsp));
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &notimplrsp));
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &okrsp));
  CheckExample();

  // Second time around, still starts from server [2], as [0] and [1] both
  // recorded failures
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &servfailrsp));
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &notimplrsp));
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &okrsp));
  CheckExample();

  // Third time around, server order is [1] (f0), [2] (f1), [0] (f2), which
  // means [1] will get called twice in a row as after the first call
  // order will be  [1] (f1), [2] (f1), [0] (f2) since sort order is
  // (failure count, index)
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &servfailrsp))
    .WillOnce(SetReply(servers_[1].get(), &notimplrsp));
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &notimplrsp));
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &okrsp));
  CheckExample();
}

TEST_P(NoRotateMultiMockTest, ServerNoResponseFailover) {
  std::vector<byte> nothing;
  DNSPacket okrsp;
  okrsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.example.com", T_A))
    .add_answer(new DNSARR("www.example.com", 100, {2,3,4,5}));

  /* Server #1 works fine on first attempt, then acts like its offline on
   * second, then backonline on the third. */
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &okrsp))
    .WillOnce(SetReplyData(servers_[0].get(), nothing))
    .WillOnce(SetReply(servers_[0].get(), &okrsp));

  /* Server #2 always acts like its offline */
  ON_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillByDefault(SetReplyData(servers_[1].get(), nothing));

  /* Server #3 works fine on first and second request, then no reply on 3rd */
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &okrsp))
    .WillOnce(SetReply(servers_[2].get(), &okrsp))
    .WillOnce(SetReplyData(servers_[2].get(), nothing));

  HostResult result;

  /* 1. First server returns a response on the first request immediately, normal
   *    operation on channel. */
  ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(0, result.timeouts_);
  std::stringstream ss1;
  ss1 << result.host_;
  EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss1.str());

  /* 2. On the second request, simulate the first and second servers not
   *    returning a response at all, but the 3rd server works, so should have
   *    2 timeouts. */
  ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(2, result.timeouts_);
  std::stringstream ss2;
  ss2 << result.host_;
  EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss2.str());

  /* 3. On the third request, the active server should be #3, so should respond
   *    immediately with no timeouts */
  ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(0, result.timeouts_);
  std::stringstream ss3;
  ss3 << result.host_;
  EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss3.str());

  /* 4. On the fourth request, the active server should be #3, but will timeout,
   *    and the first server should then respond */
  ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result);
  Process();
  EXPECT_TRUE(result.done_);
  EXPECT_EQ(1, result.timeouts_);
  std::stringstream ss4;
  ss4 << result.host_;
  EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss4.str());
}

#if defined(_WIN32)
#  define SERVER_FAILOVER_RETRY_DELAY 500
#else
#  define SERVER_FAILOVER_RETRY_DELAY 330
#endif

class ServerFailoverOptsMultiMockTest
  : public MockChannelOptsTest,
    public ::testing::WithParamInterface< std::pair<int, bool> > {
 public:
  ServerFailoverOptsMultiMockTest()
    : MockChannelOptsTest(4, GetParam().first, GetParam().second, false,
                          FillOptions(&opts_),
                          ARES_OPT_SERVER_FAILOVER | ARES_OPT_NOROTATE) {}
  void CheckExample() {
    HostResult result;
    ares_gethostbyname(channel_, "www.example.com.", AF_INET, HostCallback, &result);
    Process();
    EXPECT_TRUE(result.done_);
    std::stringstream ss;
    ss << result.host_;
    EXPECT_EQ("{'www.example.com' aliases=[] addrs=[2.3.4.5]}", ss.str());
  }

  static struct ares_options* FillOptions(struct ares_options *opts) {
    memset(opts, 0, sizeof(struct ares_options));
    opts->server_failover_opts.retry_chance = 1;
    opts->server_failover_opts.retry_delay = SERVER_FAILOVER_RETRY_DELAY;
    return opts;
  }
 private:
  struct ares_options opts_;
};


// Test case to trigger server failover behavior. We use a retry chance of
// 100% and a retry delay so that we can test behavior reliably.
TEST_P(ServerFailoverOptsMultiMockTest, ServerFailoverOpts) {
 DNSPacket servfailrsp;
  servfailrsp.set_response().set_aa().set_rcode(SERVFAIL)
    .add_question(new DNSQuestion("www.example.com", T_A));
  DNSPacket okrsp;
  okrsp.set_response().set_aa()
    .add_question(new DNSQuestion("www.example.com", T_A))
    .add_answer(new DNSARR("www.example.com", 100, {2,3,4,5}));

  auto tv_begin = std::chrono::high_resolution_clock::now();
  auto tv_now   = std::chrono::high_resolution_clock::now();
  unsigned int delay_ms;

  // At start all servers are healthy, first server should be selected
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: First server should be selected" << std::endl;
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &okrsp));
  CheckExample();

  // Fail server #0 but leave server #1 as healthy.  This results in server
  // order:
  //  #1 (failures: 0), #2 (failures: 0), #3 (failures: 0), #0 (failures: 1)
  tv_now = std::chrono::high_resolution_clock::now();
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: Server0 will fail but leave Server1 as healthy" << std::endl;
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &servfailrsp));
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &okrsp));
  CheckExample();

  // Sleep for the retry delay (actually a little more than the retry delay to account
  // for unreliable timing, e.g. NTP slew) and send in another query. The real
  // query will be sent to Server #1 (which will succeed) and Server #0 will
  // be probed and return a successful result.  This leaves the server order
  // of:
  //   #0 (failures: 0), #1 (failures: 0), #2 (failures: 0), #3 (failures: 0)
  tv_now = std::chrono::high_resolution_clock::now();
  delay_ms = SERVER_FAILOVER_RETRY_DELAY + (SERVER_FAILOVER_RETRY_DELAY / 10);
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: sleep " << delay_ms << "ms" << std::endl;
  ares_sleep_time(delay_ms);
  tv_now = std::chrono::high_resolution_clock::now();
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: Server0 should be past retry delay and should be probed (successful), server 1 will respond successful for real query" << std::endl;
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &okrsp));
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &okrsp));
  CheckExample();


  // Fail all servers for the first round of tries. On the second round, #0
  // responds successfully. This should leave server order of:
  //   #1 (failures: 0), #2 (failures: 1), #3 (failures: 1), #0 (failures: 2)
  // NOTE: A single query being retried won't spawn probes to downed servers,
  //       only an initial query attempt is eligible to spawn probes.  So
  //       no probes are sent for this test.
  tv_now = std::chrono::high_resolution_clock::now();
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: All 4 servers will fail on the first attempt, server 0 will fail on second. Server 1 will succeed on second." << std::endl;
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &servfailrsp))
    .WillOnce(SetReply(servers_[0].get(), &servfailrsp));
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &servfailrsp))
    .WillOnce(SetReply(servers_[1].get(), &okrsp));
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &servfailrsp));
  EXPECT_CALL(*servers_[3], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[3].get(), &servfailrsp));
  CheckExample();


  // Sleep for the retry delay and send in another query. Server #1 is the
  // highest priority server and will respond with success, however a probe
  // will be sent for Server #2 which will succeed:
  //  #1 (failures: 0), #2 (failures: 0), #3 (failures: 1 - expired), #0 (failures: 2 - expired)
  tv_now = std::chrono::high_resolution_clock::now();
  delay_ms = SERVER_FAILOVER_RETRY_DELAY + (SERVER_FAILOVER_RETRY_DELAY / 10);
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: sleep " << delay_ms << "ms" << std::endl;
  ares_sleep_time(delay_ms);
  tv_now = std::chrono::high_resolution_clock::now();
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: Past retry delay, will query Server 1 and probe Server 2, both will succeed." << std::endl;
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &okrsp));
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &okrsp));
  CheckExample();

  // Cause another server to fail so we have at least one non-expired failed
  // server and one expired failed server.  #1 is highest priority, which we
  // will fail, #2 will succeed, and #3 will be probed and succeed:
  //  #2 (failures: 0), #3 (failures: 0), #1 (failures: 1 not expired), #0 (failures: 2 expired)
  tv_now = std::chrono::high_resolution_clock::now();
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: Will query Server 1 and fail, Server 2 will answer successfully. Server 3 will be probed and succeed." << std::endl;
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &servfailrsp));
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &okrsp));
  EXPECT_CALL(*servers_[3], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[3].get(), &okrsp));
  CheckExample();

  // We need to make sure that if there is a failed server that is higher priority
  // but not yet expired that it will probe the next failed server instead.
  // In this case #2 is the server that the query will go to and succeed, and
  // then a probe will be sent for #0 (since #1 is not expired) and succeed.  We
  // will sleep for 1/4 the retry duration before spawning the queries so we can
  // then sleep for the rest for the follow-up test.  This will leave the servers
  // in this state:
  //   #0 (failures: 0), #2 (failures: 0), #3 (failures: 0), #1 (failures: 1 not expired)
  tv_now = std::chrono::high_resolution_clock::now();

  // We need to track retry delay time to know what is expired when.
  auto elapse_start = tv_now;

  delay_ms = (SERVER_FAILOVER_RETRY_DELAY/4);
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: sleep " << delay_ms << "ms" << std::endl;
  ares_sleep_time(delay_ms);
  tv_now = std::chrono::high_resolution_clock::now();

  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: Retry delay has not been hit yet. Server2 will be queried and succeed. Server 0 (not server 1 due to non-expired retry delay) will be probed and succeed." << std::endl;
  EXPECT_CALL(*servers_[2], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[2].get(), &okrsp));
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &okrsp));
  CheckExample();

  // Finally we sleep for the remainder of the retry delay, send another
  // query, which should succeed on Server #0, and also probe Server #1 which
  // will also succeed.
  tv_now = std::chrono::high_resolution_clock::now();

  unsigned int elapsed_time = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - elapse_start).count();
  delay_ms = (SERVER_FAILOVER_RETRY_DELAY) + (SERVER_FAILOVER_RETRY_DELAY / 10);
  if (elapsed_time > delay_ms) {
    if (verbose) std::cerr << "elapsed duration " << elapsed_time << "ms greater than desired delay of " << delay_ms << "ms, not sleeping" << std::endl;
  } else {
    delay_ms -= elapsed_time; // subtract already elapsed time
    if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: sleep " << delay_ms << "ms" << std::endl;
    ares_sleep_time(delay_ms);
  }
  tv_now = std::chrono::high_resolution_clock::now();
  if (verbose) std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(tv_now - tv_begin).count() << "ms: Retry delay has expired on Server1, Server 0 will be queried and succeed, Server 1 will be probed and succeed." << std::endl;
  EXPECT_CALL(*servers_[0], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[0].get(), &okrsp));
  EXPECT_CALL(*servers_[1], OnRequest("www.example.com", T_A))
    .WillOnce(SetReply(servers_[1].get(), &okrsp));
  CheckExample();
}

const char *af_tostr(int af)
{
  switch (af) {
    case AF_INET:
      return "ipv4";
    case AF_INET6:
      return "ipv6";
  }
  return "ipunknown";
}

const char *mode_tostr(bool mode)
{
  return mode?"ForceTCP":"DefaultUDP";
}

std::string PrintFamilyMode(const testing::TestParamInfo<std::pair<int, bool>> &info)
{
  std::string name;

  name += af_tostr(std::get<0>(info.param));
  name += "_";
  name += mode_tostr(std::get<1>(info.param));
  return name;
}

std::string PrintFamily(const testing::TestParamInfo<int> &info)
{
  std::string name;

  name += af_tostr(info.param);
  return name;
}

INSTANTIATE_TEST_SUITE_P(AddressFamilies, NoDNS0x20MockTest, ::testing::ValuesIn(ares::test::families), PrintFamily);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockChannelTest, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);

#ifdef HAVE_CONTAINER
INSTANTIATE_TEST_SUITE_P(AddressFamilies, ContainedMockChannelSysConfig, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);
#endif

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockUDPChannelTest, ::testing::ValuesIn(ares::test::families), PrintFamily);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockUDPMaxQueriesTest, ::testing::ValuesIn(ares::test::families), PrintFamily);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, CacheQueriesTest, ::testing::ValuesIn(ares::test::families), PrintFamily);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockTCPChannelTest, ::testing::ValuesIn(ares::test::families), PrintFamily);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockExtraOptsTest, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockNoCheckRespChannelTest, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);

INSTANTIATE_TEST_SUITE_P(AddressFamilies, MockEDNSChannelTest, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);

INSTANTIATE_TEST_SUITE_P(TransportModes, NoRotateMultiMockTest, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);

INSTANTIATE_TEST_SUITE_P(TransportModes, ServerFailoverOptsMultiMockTest, ::testing::ValuesIn(ares::test::families_modes), PrintFamilyMode);

}  // namespace test
}  // namespace ares
