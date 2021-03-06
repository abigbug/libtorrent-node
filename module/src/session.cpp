#include "session.hpp"

#define CORE_USER_AGENT_NAME "JoyStream"
#define CORE_PEER_ID "JS"

namespace libtorrent {
namespace node {


Nan::Persistent<v8::Function> Session::constructor;

Nan::Callback Session::_alertNotifier;

NAN_MODULE_INIT(Session::Init) {
  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Session").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "addTorrent", add_torrent);
  Nan::SetPrototypeMethod(tpl, "removeTorrent", remove_torrent);
  Nan::SetPrototypeMethod(tpl, "listenPort", listen_port);
  Nan::SetPrototypeMethod(tpl, "postTorrentUpdates", post_torrent_updates);
  Nan::SetPrototypeMethod(tpl, "pause", pause);
  Nan::SetPrototypeMethod(tpl, "isPaused", is_paused);
  Nan::SetPrototypeMethod(tpl, "resume", resume);
  Nan::SetPrototypeMethod(tpl, "findTorrent", find_torrent);
  Nan::SetPrototypeMethod(tpl, "popAlerts", pop_alerts);
  Nan::SetPrototypeMethod(tpl, "setAlertNotify", set_alert_notify);
  Nan::SetPrototypeMethod(tpl, "dhtAnnounce", dht_announce);
  Nan::SetPrototypeMethod(tpl, "dhtGetPeers", dht_get_peers);
#ifndef TORRENT_DISABLE_EXTENSIONS
  Nan::SetPrototypeMethod(tpl, "addExtension", add_extension);
#endif // TORRENT_DISABLE_EXTENSIONS

  Nan::SetPrototypeMethod(tpl, "applySettings", apply_settings);

  constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, Nan::New("Session").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

Session::Session(boost::shared_ptr<libtorrent::session> session)
: _session(session) {
  _encoders.push_back(libtorrent::node::alert_types::alertEncoder);
};

libtorrent::settings_pack Session::default_session_settings() noexcept {

    // Initialize with default values
    libtorrent::settings_pack pack;

    // Setup alert filtering
    int ignoredAlerts =
                        // Enables alerts on events in the DHT node. For incoming searches or bootstrapping being done etc.
                        libtorrent::alert::dht_notification +

                        // Enables alerts for when blocks are requested and completed. Also when pieces are completed.
                        // libtorrent::alert::progress_notification +

                        // Enables stats_alert approximately once every second, for every active torrent.
                        // These alerts contain all statistics counters for the interval since the lasts stats alert.
                        libtorrent::alert::stats_notification +

                        // Enables debug logging alerts. These are available unless libtorrent was
                        // built with logging disabled (TORRENT_DISABLE_LOGGING).
                        // The alerts being posted are log_alert and are session wide.
                        libtorrent::alert::session_log_notification +

                        // Enables debug logging alerts for torrents. These are available unless libtorrent was
                        // built with logging disabled (TORRENT_DISABLE_LOGGING). The alerts being posted are
                        // torrent_log_alert and are torrent wide debug events.
                        libtorrent::alert::torrent_log_notification +

                        // Enables debug logging alerts for peers. These are available unless libtorrent was
                        // built with logging disabled (TORRENT_DISABLE_LOGGING). The alerts being posted are
                        // peer_log_alert and low-level peer events and messages.
                        libtorrent::alert::peer_log_notification +

                        // Enables dht_log_alert, debug logging for the DHT
                        libtorrent::alert::dht_log_notification +

                        // Enables verbose logging from the piece picker
                        libtorrent::alert::picker_log_notification +

                        // Enables logging of port mapping events - for debugging the UPnP or NAT-PMP implementation
                        libtorrent::alert::port_mapping_log_notification;

    pack.set_int(libtorrent::settings_pack::alert_mask, libtorrent::alert::all_categories & ~ ignoredAlerts);

    // Disable all default extensions
    pack.set_bool(libtorrent::settings_pack::enable_upnp, false);
    pack.set_bool(libtorrent::settings_pack::enable_natpmp, false);
    pack.set_bool(libtorrent::settings_pack::enable_lsd, false);
    pack.set_bool(libtorrent::settings_pack::enable_dht, false);

    // This is the client identification to the tracker.
    // The recommended format of this string is: "ClientName/ClientVersion libtorrent/libtorrentVersion".
    // This name will not only be used when making HTTP requests, but also when sending BEP10 extended handshake
    // if handshake_client_version is left blank.
    // default: "libtorrent/" LIBTORRENT_VERSION
    pack.set_str(libtorrent::settings_pack::user_agent, std::string(CORE_USER_AGENT_NAME));

    // Client name and version identifier sent to peers in the BEP10 handshake message.
    // If this is an empty string, the user_agent is used instead.
    // default: <user_agent>
    //pack.set_str(libtorrent::settings_pack::handshake_client_version, std::string(CORE_USER_AGENT_NAME) + CORE_VERSION_MAJOR + "." + CORE_VERSION_MINOR);

    // Fingerprint for the client.
    // It will be used as the prefix to the peer_id.
    // If this is 20 bytes (or longer) it will be used as the peer-id
    // There are two encoding styles, we use Azureus style, which is most popular:
    // '-', two characters for client id, four ascii digits for version number, '-', followed by random numbers.
    // For example: '-AZ2060-'...
    // default: "-LT1100-"
    std::string peerIdString = libtorrent::fingerprint(CORE_PEER_ID, 0, 0, 0, 0).to_string();

    pack.set_str(libtorrent::settings_pack::peer_fingerprint, peerIdString);

    // Determines if connections from the same IP address as existing
    // connections should be rejected or not. Multiple connections from
    // the same IP address is not allowed by default, to prevent abusive behavior by peers.
    // It may be useful to allow such connections in cases where simulations
    // are run on the same machie, and all peers in a swarm has the same IP address.
    pack.set_bool(libtorrent::settings_pack::allow_multiple_connections_per_ip, false);

    // Default alert queue size (1000) might be too small, bumping it up to avoid dropped alerts
    // A high rate of alerts can happen when adding many large torrents as they are being checked
    pack.set_int(libtorrent::settings_pack::alert_queue_size, 5000);

    // Add DHT routers
    pack.set_str(libtorrent::settings_pack::dht_bootstrap_nodes,
      "router.bittorrent.com:6881,router.utorrent.com:6881,dht.libtorrent.org:25401,dht.transmissionbt.com:6881,dht.aelitis.com:6881");

    pack.set_str(libtorrent::settings_pack::listen_interfaces, "0.0.0.0:7881");

    return pack;
}

void Session::update_settings(const v8::Local<v8::Value> & settings, libtorrent::settings_pack &pack) {
  if(!settings->IsObject())
    throw std::runtime_error("Argument must be dictionary.");

  v8::Local<v8::Object> o = settings->ToObject();

  if (HAS_KEY(o, "listen_interfaces")) {
    auto value = GET_STD_STRING(o, "listen_interfaces");
    pack.set_str(libtorrent::settings_pack::listen_interfaces, value);
  }

  if (HAS_KEY(o, "outgoing_interfaces")) {
    auto value = GET_STD_STRING(o, "outgoing_interfaces");
    pack.set_str(libtorrent::settings_pack::outgoing_interfaces, value);
  }

  if (HAS_KEY(o, "proxy_type")) {
    auto value = GET_INT32(o, "proxy_type");
    pack.set_int(libtorrent::settings_pack::proxy_type, value);
  }

  if (HAS_KEY(o, "proxy_hostname")) {
    auto value = GET_STD_STRING(o, "proxy_hostname");
    pack.set_str(libtorrent::settings_pack::proxy_hostname, value);
  }

  if (HAS_KEY(o, "proxy_username")) {
    auto value = GET_STD_STRING(o, "proxy_username");
    pack.set_str(libtorrent::settings_pack::proxy_username, value);
  }

  if (HAS_KEY(o, "proxy_password")) {
    auto value = GET_STD_STRING(o, "proxy_password");
    pack.set_str(libtorrent::settings_pack::proxy_password, value);
  }

  if (HAS_KEY(o, "proxy_port")) {
    auto value = GET_INT32(o, "proxy_port");
    pack.set_int(libtorrent::settings_pack::proxy_port, value);
  }

  if (HAS_KEY(o, "i2p_port")) {
    auto value = GET_INT32(o, "i2p_port");
    pack.set_int(libtorrent::settings_pack::i2p_port, value);
  }

  if (HAS_KEY(o, "enable_dht")) {
    auto enable = GET_BOOL(o, "enable_dht");
    pack.set_bool(libtorrent::settings_pack::enable_dht, enable);
  }

  if (HAS_KEY(o, "allow_multiple_connections_per_ip")) {
    auto enable = GET_BOOL(o, "allow_multiple_connections_per_ip");
    pack.set_bool(libtorrent::settings_pack::allow_multiple_connections_per_ip, enable);
  }

  if (HAS_KEY(o, "enable_upnp")) {
    auto enable = GET_BOOL(o, "enable_upnp");
    pack.set_bool(libtorrent::settings_pack::enable_upnp, enable);
  }

  if (HAS_KEY(o, "enable_natpmp")) {
    auto enable = GET_BOOL(o, "enable_natpmp");
    pack.set_bool(libtorrent::settings_pack::enable_natpmp, enable);
  }

  if (HAS_KEY(o, "enable_lsd")) {
    auto enable = GET_BOOL(o, "enable_lsd");
    pack.set_bool(libtorrent::settings_pack::enable_lsd, enable);
  }

  if (HAS_KEY(o, "anonymous_mode")) {
    auto enable = GET_BOOL(o, "anonymous_mode");
    pack.set_bool(libtorrent::settings_pack::anonymous_mode, enable);
  }

  if (HAS_KEY(o, "force_proxy")) {
    auto enable = GET_BOOL(o, "force_proxy");
    pack.set_bool(libtorrent::settings_pack::force_proxy, enable);
  }

  if (HAS_KEY(o, "user_agent")) {
    auto value = GET_STD_STRING(o, "user_agent");
    pack.set_str(libtorrent::settings_pack::user_agent, value);
  }

  if (HAS_KEY(o, "peer_fingerprint")) {
    auto fingerprint = GET_VAL(o, "peer_fingerprint");

    if (fingerprint->IsObject()) {
      auto value = fingerprint->ToObject();
      auto name = GET_STD_STRING(value, "name");
      auto major = GET_INT32(value, "major");
      auto minor = GET_INT32(value, "minor");
      auto revision = GET_INT32(value, "revision");
      auto tag = GET_INT32(value, "tag");

      std::string peerIdString = libtorrent::fingerprint(name.c_str(), major, minor, revision, tag).to_string();
      pack.set_str(libtorrent::settings_pack::peer_fingerprint, peerIdString);
    }
  }

  if (HAS_KEY(o, "alert_mask")) {
    auto value = GET_INT32(o, "alert_mask");
    pack.set_int(libtorrent::settings_pack::alert_mask, value);
  }

}

NAN_METHOD(Session::New) {
  if (!info.IsConstructCall()) return;

  boost::shared_ptr<libtorrent::session> session;

  libtorrent::settings_pack session_settings = default_session_settings();
  libtorrent::dht_settings dht_settings;

  if (ARGUMENTS_IS_OBJECT(0)) {
    update_settings(info[0], session_settings);
  }

  session = boost::shared_ptr<libtorrent::session>(new libtorrent::session(session_settings));

  session->set_dht_settings(dht_settings);

  Session* obj = new Session(session);
  obj->Wrap(info.This());
  RETURN(info.This());
}

NAN_METHOD(Session::add_torrent) {
  REQUIRE_ARGUMENTS(1);

  libtorrent::torrent_handle th;
  libtorrent::error_code ec;

  ARGUMENTS_REQUIRE_DECODED(0, params, libtorrent::add_torrent_params, libtorrent::node::add_torrent_params::decode);

  Session* session = Nan::ObjectWrap::Unwrap<Session>(info.This());

  th = session->_session->add_torrent(params, ec);

  RETURN(TorrentHandle::New(th));
}

NAN_METHOD(Session::remove_torrent) {
  REQUIRE_ARGUMENTS(1);

  EXPLOSIVE_ARGUMENT_REQUIRE_WRAPS(0, TorrentHandle, th);

  Session* session = Nan::ObjectWrap::Unwrap<Session>(info.This());

  session->_session->remove_torrent(th->th_);

  RETURN_VOID;
}


NAN_METHOD(Session::listen_port) {
  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  RETURN(session->_session->listen_port());
}

NAN_METHOD(Session::post_torrent_updates) {
  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  session->_session->post_torrent_updates();

  RETURN_VOID;
}

NAN_METHOD(Session::pause) {
  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  session->_session->resume();

  RETURN_VOID;
}

NAN_METHOD(Session::is_paused) {
  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  v8::Local<v8::Boolean> isPaused;
  if (session->_session->is_paused()) {
    isPaused = Nan::True();
  } else {
    isPaused = Nan::False();
  }

  RETURN(isPaused);
}

NAN_METHOD(Session::resume) {
  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  session->_session->resume();

  RETURN_VOID;
}

NAN_METHOD(Session::find_torrent) {
  libtorrent::torrent_handle th;

  ARGUMENTS_REQUIRE_DECODED(0,info_hash, libtorrent::sha1_hash, libtorrent::node::sha1_hash::decode);

  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  th = session->_session->find_torrent(info_hash);

  RETURN(TorrentHandle::New(th));
}

#ifndef TORRENT_DISABLE_EXTENSIONS
NAN_METHOD(Session::add_extension) {

  // Recover the plugin binding
  // ***** USER MUST SUPPLY WRAPPED OBJECT OF CORRECT KIND, OR V8 DIES *****
  EXPLOSIVE_ARGUMENT_REQUIRE_WRAPS(0, libtorrent::node::plugin, p)

  // Recover session binding
  Session * session = Nan::ObjectWrap::Unwrap<Session>(info.This());

  // Add underlying plugin to underlying session
  session->_session->add_extension(p->getPlugin());

  // Get alert converter for plugin, and add it to list of converters.
  session->_encoders.push_back(p->getEncoder());
}
#endif // TORRENT_DISABLE_EXTENSIONS

NAN_METHOD(Session::pop_alerts) {

  // Recover session binding
  Session * session = Nan::ObjectWrap::Unwrap<Session>(info.This());

  // Get currently pending alerts from libtorrent
  std::vector<libtorrent::alert*> alerts;
  session->_session->pop_alerts(&alerts);

  // Iterate alerts, and convert to js objects
  v8::Local<v8::Array> ret = Nan::New<v8::Array>();
  for(const libtorrent::alert * alert : alerts) {

    // Iterate encoders to find match
    for(AlertEncoder encoder : session->_encoders) {

      // encode
      auto v = encoder(alert);

      // if encoded, then store, and break to next alert
      if(v.is_initialized()) {
        ret->Set(ret->Length(), v.get());
        break;
      }

    }
  }

  RETURN(ret);
}

NAN_METHOD(Session::set_alert_notify) {

  ARGUMENTS_REQUIRE_FUNCTION(0, fn);

  // Recover session binding
  Session* session = Nan::ObjectWrap::Unwrap<Session>(info.This());

  // Store persistent handle to callback
  _alertNotifier.Reset(fn);

  // Set alert notifier on libtorrent session
  session->_session->set_alert_notify([]() { _alertNotifier(0, {}); });
}

NAN_METHOD(Session::dht_announce) {
  REQUIRE_ARGUMENTS(2);

  ARGUMENTS_REQUIRE_DECODED(0,info_hash, libtorrent::sha1_hash, libtorrent::node::sha1_hash::decode);

  unsigned int listen_port = info[1]->Uint32Value();


  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  session->_session->dht_announce(info_hash, listen_port);

  RETURN_VOID;
}

NAN_METHOD(Session::dht_get_peers) {
  REQUIRE_ARGUMENTS(1);

  ARGUMENTS_REQUIRE_DECODED(0,info_hash, libtorrent::sha1_hash, libtorrent::node::sha1_hash::decode);

  Session* session = ObjectWrap::Unwrap<Session>(info.This());

  session->_session->dht_get_peers(info_hash);

  RETURN_VOID;
}

NAN_METHOD(Session::apply_settings) {
  REQUIRE_ARGUMENTS(1);

  if (ARGUMENTS_IS_OBJECT(0)) {

    Session* session = ObjectWrap::Unwrap<Session>(info.This());

    libtorrent::settings_pack pack;

    update_settings(info[0], pack);

    session->_session->apply_settings(pack);
  }

  RETURN_VOID;
}

}
}
