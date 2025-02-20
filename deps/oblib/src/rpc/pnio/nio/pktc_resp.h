
static void pktc_do_cb(pktc_cb_t* cb, pktc_msg_t* m) {
  cb->resp_cb(cb, m->payload, m->sz);
}

static void pktc_do_cb_exception(pktc_cb_t* cb) {
  cb->resp_cb(cb, NULL, 0);
}

static void pktc_resp_cb_on_sk_destroy(pktc_t* io, pktc_sk_t* s) {
  dlink_for(&s->cb_head, p) {
    pktc_cb_t* cb = structof(p, pktc_cb_t, sk_dlink);
    ihash_del(&io->cb_map, cb->id);
    dlink_delete(&cb->timer_dlink);
    rk_info("resp_cb on sk_destroy: packet_id=%lu s=%p", cb->id, s);
    cb->errcode = PNIO_DISCONNECT;
    pktc_do_cb_exception(cb);
  }
}

static void pktc_resp_cb_on_timeout(time_wheel_t* tw, dlink_t* l) {
  pktc_cb_t* cb = structof(l, pktc_cb_t, timer_dlink);
  pktc_t* io = structof(tw, pktc_t, cb_tw);
  ihash_del(&io->cb_map, cb->id);
  dlink_delete(&cb->sk_dlink);
  rk_info("resp_cb on timeout: packet_id=%lu expire_us=%ld", cb->id, cb->expire_us);
  cb->errcode = PNIO_TIMEOUT;
  pktc_do_cb_exception(cb);
}

static void pktc_resp_cb_on_post_fail(pktc_req_t* r) {
  r->resp_cb->errcode = PNIO_CONNECT_FAIL;
  pktc_do_cb_exception(r->resp_cb);
}

static void pktc_resp_cb_on_msg(pktc_t* io, pktc_msg_t* msg) {
  uint64_t id = pktc_get_id(msg);
  link_t* hlink = ihash_del(&io->cb_map, id);
  if (hlink) {
    pktc_cb_t* cb = structof(hlink, pktc_cb_t, hash_link);
    dlink_delete(&cb->timer_dlink);
    dlink_delete(&cb->sk_dlink);
    pktc_do_cb(cb, msg);
  } else {
    rk_info("resp cb not found: packet_id=%lu", id);
  }
}

static int pktc_sk_handle_msg(pktc_sk_t* s, pktc_msg_t* m) {
  pktc_t* io = structof(s->fty, pktc_t, sf);
  pktc_resp_cb_on_msg(io, m);
  ib_consumed(&s->ib, m->sz);
  return 0;
}
