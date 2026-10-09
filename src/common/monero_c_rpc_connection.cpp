/**
 * Copyright (c) Libmonero
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "common/monero_c_rpc_connection.h"
#include "common/monero_c_common.h"
#include "common/monero_rpc_connection.h"

#include "rapidjson/document.h"

#include <boost/property_tree/ptree.hpp>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

// monero-cpp has `using namespace monero`, so the C connection name is written with ::
struct monero_rpc_connection {
  std::shared_ptr<monero::monero_rpc_connection> connection;
};

namespace {

using monero_c::check_json_depth;
using monero_c::dup_buffer;
using monero_c::dup_string;
using monero_c::guard;
using monero_c::optional_of;
using monero_c::require;
using monero_c::reset_out;
using monero_c::safe_str;

// request parameters given as a JSON object, parsed once
struct json_params : public monero::monero_request_params {
  rapidjson::Document m_doc;

  explicit json_params(const char* json) {
    check_json_depth(json);
    if (m_doc.Parse(json).HasParseError() || !m_doc.IsObject()) throw std::runtime_error("params must be a JSON object");
  }

  rapidjson::Value to_rapidjson_val(rapidjson::Document::AllocatorType& allocator) const override {
    return rapidjson::Value(m_doc, allocator);
  }
};

std::shared_ptr<monero::monero_request_params> params_of(const char* params_json) {
  if (params_json == nullptr) return nullptr;
  return std::make_shared<json_params>(params_json);
}

monero_optional_bool optional_bool_of(const boost::optional<bool>& value) {
  if (value == boost::none) return MONERO_OPTIONAL_BOOL_UNSET;
  return *value ? MONERO_OPTIONAL_BOOL_TRUE : MONERO_OPTIONAL_BOOL_FALSE;
}

// true if text follows the JSON number grammar: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
bool is_json_number(const std::string& text) {
  size_t i = 0;
  size_t n = text.size();
  if (i < n && text[i] == '-') i++;
  if (i >= n || text[i] < '0' || text[i] > '9') return false;
  if (text[i] == '0') i++;
  else while (i < n && text[i] >= '0' && text[i] <= '9') i++;
  if (i < n && text[i] == '.') {
    i++;
    if (i >= n || text[i] < '0' || text[i] > '9') return false;
    while (i < n && text[i] >= '0' && text[i] <= '9') i++;
  }
  if (i < n && (text[i] == 'e' || text[i] == 'E')) {
    i++;
    if (i < n && (text[i] == '+' || text[i] == '-')) i++;
    if (i >= n || text[i] < '0' || text[i] > '9') return false;
    while (i < n && text[i] >= '0' && text[i] <= '9') i++;
  }
  return i == n;
}

// writes a property tree value with the type its text suggests. Integers keep their 64 bits,
// and a number too large for 64 bits stays a string
void write_value(const std::string& text, rapidjson::Writer<rapidjson::StringBuffer>& writer) {
  if (text == "true") { writer.Bool(true); return; }
  if (text == "false") { writer.Bool(false); return; }
  if (text == "null") { writer.Null(); return; }
  if (is_json_number(text)) {
    bool integer = text.find_first_of(".eE") == std::string::npos;
    char* end = nullptr;
    errno = 0;
    if (integer && text[0] == '-') {
      long long value = std::strtoll(text.c_str(), &end, 10);
      if (errno == 0) { writer.Int64(static_cast<int64_t>(value)); return; }
    } else if (integer) {
      unsigned long long value = std::strtoull(text.c_str(), &end, 10);
      if (errno == 0) { writer.Uint64(static_cast<uint64_t>(value)); return; }
    } else {
      double value = std::strtod(text.c_str(), &end);
      if (errno == 0) { writer.Double(value); return; }
    }
  }
  writer.String(text.c_str(), static_cast<rapidjson::SizeType>(text.size()));
}

// a property tree has no types and no empty arrays, so a node whose children have no keys is an array
void write_tree(const boost::property_tree::ptree& tree, rapidjson::Writer<rapidjson::StringBuffer>& writer) {
  if (tree.empty()) {
    write_value(tree.data(), writer);
    return;
  }
  bool is_array = true;
  for (const auto& child : tree) {
    if (!child.first.empty()) {
      is_array = false;
      break;
    }
  }
  if (is_array) {
    writer.StartArray();
    for (const auto& child : tree) write_tree(child.second, writer);
    writer.EndArray();
  } else {
    writer.StartObject();
    for (const auto& child : tree) {
      writer.Key(child.first.c_str(), static_cast<rapidjson::SizeType>(child.first.size()));
      write_tree(child.second, writer);
    }
    writer.EndObject();
  }
}

std::string json_of_tree(const boost::optional<boost::property_tree::ptree>& tree) {
  if (tree == boost::none) throw std::runtime_error("Invalid Monero RPC response");
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  write_tree(*tree, writer);
  return buffer.GetString();
}

} // namespace

namespace monero_c {

std::shared_ptr<monero::monero_rpc_connection> connection_of(::monero_rpc_connection* connection) {
  if (connection == nullptr) throw std::runtime_error("connection must not be null");
  return connection->connection;
}

} // namespace monero_c

extern "C" {

monero_result monero_rpc_connection_create(const char* connection_json, ::monero_rpc_connection** out_connection) {
  reset_out(out_connection);
  if (!require(connection_json, "connection_json") || !require(out_connection, "out_connection")) return MONERO_ERROR;
  return guard([&] {
    std::unique_ptr<::monero_rpc_connection> handle(new ::monero_rpc_connection());
    std::string json(connection_json);
    check_json_depth(json);
    handle->connection = monero::monero_rpc_connection::deserialize(json);
    *out_connection = handle.release();
  });
}

void monero_rpc_connection_free(::monero_rpc_connection* connection) {
  delete connection;
}

monero_result monero_rpc_connection_serialize(::monero_rpc_connection* connection, char** out_json) {
  reset_out(out_json);
  if (!require(connection, "connection") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(connection->connection->serialize()); });
}

monero_result monero_rpc_connection_set_credentials(::monero_rpc_connection* connection, const char* username, const char* password) {
  if (!require(connection, "connection")) return MONERO_ERROR;
  return guard([&] { connection->connection->set_credentials(safe_str(username), safe_str(password)); });
}

monero_result monero_rpc_connection_set_attribute(::monero_rpc_connection* connection, const char* key, const char* value) {
  if (!require(connection, "connection") || !require(key, "key") || !require(value, "value")) return MONERO_ERROR;
  return guard([&] { connection->connection->set_attribute(std::string(key), std::string(value)); });
}

monero_result monero_rpc_connection_get_attribute(::monero_rpc_connection* connection, const char* key, char** out_value) {
  reset_out(out_value);
  if (!require(connection, "connection") || !require(key, "key") || !require(out_value, "out_value")) return MONERO_ERROR;
  return guard([&] { *out_value = dup_string(connection->connection->get_attribute(std::string(key))); });
}

monero_result monero_rpc_connection_is_onion(::monero_rpc_connection* connection, bool* out_is_onion) {
  if (!require(connection, "connection") || !require(out_is_onion, "out_is_onion")) return MONERO_ERROR;
  return guard([&] { *out_is_onion = connection->connection->is_onion(); });
}

monero_result monero_rpc_connection_is_i2p(::monero_rpc_connection* connection, bool* out_is_i2p) {
  if (!require(connection, "connection") || !require(out_is_i2p, "out_is_i2p")) return MONERO_ERROR;
  return guard([&] { *out_is_i2p = connection->connection->is_i2p(); });
}

monero_result monero_rpc_connection_is_online(::monero_rpc_connection* connection, monero_optional_bool* out_is_online) {
  if (!require(connection, "connection") || !require(out_is_online, "out_is_online")) return MONERO_ERROR;
  return guard([&] { *out_is_online = optional_bool_of(connection->connection->is_online()); });
}

monero_result monero_rpc_connection_is_authenticated(::monero_rpc_connection* connection, monero_optional_bool* out_is_authenticated) {
  if (!require(connection, "connection") || !require(out_is_authenticated, "out_is_authenticated")) return MONERO_ERROR;
  return guard([&] { *out_is_authenticated = optional_bool_of(connection->connection->is_authenticated()); });
}

monero_result monero_rpc_connection_is_connected(::monero_rpc_connection* connection, monero_optional_bool* out_is_connected) {
  if (!require(connection, "connection") || !require(out_is_connected, "out_is_connected")) return MONERO_ERROR;
  return guard([&] { *out_is_connected = optional_bool_of(connection->connection->is_connected()); });
}

monero_result monero_rpc_connection_check_connection(::monero_rpc_connection* connection, const uint32_t* timeout_ms, bool* out_changed) {
  if (!require(connection, "connection") || !require(out_changed, "out_changed")) return MONERO_ERROR;
  return guard([&] { *out_changed = connection->connection->check_connection(optional_of(timeout_ms)); });
}

monero_result monero_rpc_connection_send_json_request(::monero_rpc_connection* connection, const char* method, const char* params_json, const uint32_t* timeout_ms, char** out_json) {
  reset_out(out_json);
  if (!require(connection, "connection") || !require(method, "method") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_rpc_request request(std::string(method), params_of(params_json));
    *out_json = dup_string(json_of_tree(connection->connection->send_json_request(request, optional_of(timeout_ms)).m_result));
  });
}

monero_result monero_rpc_connection_send_path_request(::monero_rpc_connection* connection, const char* path, const char* params_json, const uint32_t* timeout_ms, char** out_json) {
  reset_out(out_json);
  if (!require(connection, "connection") || !require(path, "path") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_rpc_request request(std::string(path), params_of(params_json), false);
    *out_json = dup_string(json_of_tree(connection->connection->send_path_request(request, optional_of(timeout_ms)).m_response));
  });
}

monero_result monero_rpc_connection_send_binary_request(::monero_rpc_connection* connection, const char* path, const char* params_json, const uint32_t* timeout_ms, uint8_t** out_data, size_t* out_len) {
  reset_out(out_data);
  reset_out(out_len);
  if (!require(connection, "connection") || !require(path, "path") || !require(out_data, "out_data") || !require(out_len, "out_len")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_rpc_request request(std::string(path), params_of(params_json), false);
    monero::monero_rpc_response response = connection->connection->send_binary_request(request, optional_of(timeout_ms));
    *out_data = dup_buffer(response.m_binary.value_or(std::string()), out_len);
  });
}

} // extern "C"
