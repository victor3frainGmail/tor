/* Copyright (c) 2007-2018, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/**
 * \file test_nodelist.c
 * \brief Unit tests for nodelist related functions.
 **/

#define NODELIST_PRIVATE

#include "core/or/or.h"
#include "lib/crypt_ops/crypto_rand.h"
#include "feature/nodelist/networkstatus.h"
#include "feature/nodelist/nodefamily.h"
#include "feature/nodelist/nodelist.h"
#include "feature/nodelist/torcert.h"

#include "feature/nodelist/microdesc_st.h"
#include "feature/nodelist/networkstatus_st.h"
#include "feature/nodelist/node_st.h"
#include "feature/nodelist/nodefamily_st.h"
#include "feature/nodelist/routerinfo_st.h"
#include "feature/nodelist/routerstatus_st.h"

#include "test/test.h"
#include "test/log_test_helpers.h"

/** Test the case when node_get_by_id() returns NULL,
 * node_get_verbose_nickname_by_id should return the base 16 encoding
 * of the id.
 */
static void
test_nodelist_node_get_verbose_nickname_by_id_null_node(void *arg)
{
  char vname[MAX_VERBOSE_NICKNAME_LEN+1];
  const char ID[] = "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
                    "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA";
  (void) arg;

  /* make sure node_get_by_id returns NULL */
  tt_assert(!node_get_by_id(ID));
  node_get_verbose_nickname_by_id(ID, vname);
  tt_str_op(vname,OP_EQ, "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
 done:
  return;
}

/** For routers without named flag, get_verbose_nickname should return
 * "Fingerprint~Nickname"
 */
static void
test_nodelist_node_get_verbose_nickname_not_named(void *arg)
{
  node_t mock_node;
  routerstatus_t mock_rs;

  char vname[MAX_VERBOSE_NICKNAME_LEN+1];

  (void) arg;

  memset(&mock_node, 0, sizeof(node_t));
  memset(&mock_rs, 0, sizeof(routerstatus_t));

  /* verbose nickname should use ~ instead of = for unnamed routers */
  strlcpy(mock_rs.nickname, "TestOR", sizeof(mock_rs.nickname));
  mock_node.rs = &mock_rs;
  memcpy(mock_node.identity,
          "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
          "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA",
          DIGEST_LEN);
  node_get_verbose_nickname(&mock_node, vname);
  tt_str_op(vname,OP_EQ, "$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA~TestOR");

 done:
  return;
}

/** A node should be considered a directory server if it has an open dirport
 * of it accepts tunnelled directory requests.
 */
static void
test_nodelist_node_is_dir(void *arg)
{
  (void)arg;

  routerstatus_t rs;
  routerinfo_t ri;
  node_t node;
  memset(&node, 0, sizeof(node_t));
  memset(&rs, 0, sizeof(routerstatus_t));
  memset(&ri, 0, sizeof(routerinfo_t));

  tt_assert(!node_is_dir(&node));

  node.rs = &rs;
  tt_assert(!node_is_dir(&node));

  rs.is_v2_dir = 1;
  tt_assert(node_is_dir(&node));

  rs.is_v2_dir = 0;
  rs.dir_port = 1;
  tt_assert(! node_is_dir(&node));

  node.rs = NULL;
  tt_assert(!node_is_dir(&node));
  node.ri = &ri;
  ri.supports_tunnelled_dir_requests = 1;
  tt_assert(node_is_dir(&node));
  ri.supports_tunnelled_dir_requests = 0;
  ri.dir_port = 1;
  tt_assert(! node_is_dir(&node));

 done:
  return;
}

static networkstatus_t *dummy_ns = NULL;
static networkstatus_t *
mock_networkstatus_get_latest_consensus(void)
{
  return dummy_ns;
}
static networkstatus_t *
mock_networkstatus_get_latest_consensus_by_flavor(consensus_flavor_t f)
{
  tor_assert(f == FLAV_MICRODESC);
  return dummy_ns;
}

static void
test_nodelist_ed_id(void *arg)
{
#define N_NODES 5
  routerstatus_t *rs[N_NODES];
  microdesc_t *md[N_NODES];
  routerinfo_t *ri[N_NODES];
  networkstatus_t *ns;
  int i;
  (void)arg;

  ns = tor_malloc_zero(sizeof(networkstatus_t));
  ns->flavor = FLAV_MICRODESC;
  ns->routerstatus_list = smartlist_new();
  dummy_ns = ns;
  MOCK(networkstatus_get_latest_consensus,
       mock_networkstatus_get_latest_consensus);
  MOCK(networkstatus_get_latest_consensus_by_flavor,
       mock_networkstatus_get_latest_consensus_by_flavor);

  /* Make a bunch of dummy objects that we can play around with.  Only set the
     necessary fields */

  for (i = 0; i < N_NODES; ++i) {
    rs[i] = tor_malloc_zero(sizeof(*rs[i]));
    md[i] = tor_malloc_zero(sizeof(*md[i]));
    ri[i] = tor_malloc_zero(sizeof(*ri[i]));

    crypto_rand(md[i]->digest, sizeof(md[i]->digest));
    md[i]->ed25519_identity_pkey = tor_malloc(sizeof(ed25519_public_key_t));
    crypto_rand((char*)md[i]->ed25519_identity_pkey,
                sizeof(ed25519_public_key_t));
    crypto_rand(rs[i]->identity_digest, sizeof(rs[i]->identity_digest));
    memcpy(ri[i]->cache_info.identity_digest, rs[i]->identity_digest,
           DIGEST_LEN);
    memcpy(rs[i]->descriptor_digest, md[i]->digest, DIGEST256_LEN);
    ri[i]->cache_info.signing_key_cert = tor_malloc_zero(sizeof(tor_cert_t));
    memcpy(&ri[i]->cache_info.signing_key_cert->signing_key,
           md[i]->ed25519_identity_pkey, sizeof(ed25519_public_key_t));

    if (i < 3)
      smartlist_add(ns->routerstatus_list, rs[i]);
  }

  tt_int_op(0, OP_EQ, smartlist_len(nodelist_get_list()));

  nodelist_set_consensus(ns);

  tt_int_op(3, OP_EQ, smartlist_len(nodelist_get_list()));

  /* No Ed25519 info yet, so nothing has an ED id. */
  tt_ptr_op(NULL, OP_EQ, node_get_by_ed25519_id(md[0]->ed25519_identity_pkey));

  /* Register the first one by md, then look it up. */
  node_t *n = nodelist_add_microdesc(md[0]);
  tt_ptr_op(n, OP_EQ, node_get_by_ed25519_id(md[0]->ed25519_identity_pkey));

  /* Register the second by ri, then look it up. */
  routerinfo_t *ri_old = NULL;
  n = nodelist_set_routerinfo(ri[1], &ri_old);
  tt_ptr_op(n, OP_EQ, node_get_by_ed25519_id(md[1]->ed25519_identity_pkey));
  tt_ptr_op(ri_old, OP_EQ, NULL);

  /* Register it by md too. */
  node_t *n2 = nodelist_add_microdesc(md[1]);
  tt_ptr_op(n2, OP_EQ, n);
  tt_ptr_op(n, OP_EQ, node_get_by_ed25519_id(md[1]->ed25519_identity_pkey));

  /* Register the 4th by ri only -- we never put it into the networkstatus,
   * so it has to be independent */
  node_t *n3 = nodelist_set_routerinfo(ri[3], &ri_old);
  tt_ptr_op(n3, OP_EQ, node_get_by_ed25519_id(md[3]->ed25519_identity_pkey));
  tt_ptr_op(ri_old, OP_EQ, NULL);
  tt_int_op(4, OP_EQ, smartlist_len(nodelist_get_list()));

  /* Register the 5th by ri only, and rewrite its ed25519 pubkey to be
   * the same as the 4th, to test the duplicate ed25519 key logging in
   * nodelist.c */
  memcpy(md[4]->ed25519_identity_pkey, md[3]->ed25519_identity_pkey,
         sizeof(ed25519_public_key_t));
  memcpy(&ri[4]->cache_info.signing_key_cert->signing_key,
         md[3]->ed25519_identity_pkey, sizeof(ed25519_public_key_t));

  setup_capture_of_logs(LOG_NOTICE);
  node_t *n4 = nodelist_set_routerinfo(ri[4], &ri_old);
  tt_ptr_op(ri_old, OP_EQ, NULL);
  tt_int_op(5, OP_EQ, smartlist_len(nodelist_get_list()));
  tt_ptr_op(n4, OP_NE, node_get_by_ed25519_id(md[3]->ed25519_identity_pkey));
  tt_ptr_op(n3, OP_EQ, node_get_by_ed25519_id(md[3]->ed25519_identity_pkey));
  expect_log_msg_containing("Reused ed25519_id");

 done:
  teardown_capture_of_logs();
  for (i = 0; i < N_NODES; ++i) {
    tor_free(rs[i]);
    tor_free(md[i]->ed25519_identity_pkey);
    tor_free(md[i]);
    tor_free(ri[i]->cache_info.signing_key_cert);
    tor_free(ri[i]);
  }
  smartlist_clear(ns->routerstatus_list);
  networkstatus_vote_free(ns);
  UNMOCK(networkstatus_get_latest_consensus);
  UNMOCK(networkstatus_get_latest_consensus_by_flavor);
#undef N_NODES
}

static void
test_nodelist_nodefamily(void *arg)
{
  (void)arg;
  /* hex ID digests */
  const char h1[] = "5B435D6869206861206C65207363617270652070";
  const char h2[] = "75C3B220616E6461726520696E206769726F2061";
  const char h3[] = "2074726F766172206461206D616E67696172652C";
  const char h4[] = "206D656E747265206E6F6E2076616C65206C2769";
  const char h5[] = "6E766572736F2E202D2D5072696D6F204C657669";

  /* binary ID digests */
  uint8_t d1[DIGEST_LEN], d2[DIGEST_LEN], d3[DIGEST_LEN], d4[DIGEST_LEN],
    d5[DIGEST_LEN];
  base16_decode((char*)d1, sizeof(d1), h1, strlen(h1));
  base16_decode((char*)d2, sizeof(d2), h2, strlen(h2));
  base16_decode((char*)d3, sizeof(d3), h3, strlen(h3));
  base16_decode((char*)d4, sizeof(d4), h4, strlen(h4));
  base16_decode((char*)d5, sizeof(d5), h5, strlen(h5));

  char *enc=NULL, *enc2=NULL;

  nodefamily_t *nf1 = NULL;
  nodefamily_t *nf2 = NULL;
  nodefamily_t *nf3 = NULL;

  enc = nodefamily_format(NULL);
  tt_str_op(enc, OP_EQ, "");
  tor_free(enc);

  /* Make sure that sorting and de-duplication work. */
  tor_asprintf(&enc, "$%s hello", h1);
  nf1 = nodefamily_parse(enc, NULL, 0);
  tt_assert(nf1);
  tor_free(enc);

  tor_asprintf(&enc, "hello hello $%s hello", h1);
  nf2 = nodefamily_parse(enc, NULL, 0);
  tt_assert(nf2);
  tt_ptr_op(nf1, OP_EQ, nf2);
  tor_free(enc);

  tor_asprintf(&enc, "%s $%s hello", h1, h1);
  nf3 = nodefamily_parse(enc, NULL, 0);
  tt_assert(nf3);
  tt_ptr_op(nf1, OP_EQ, nf3);
  tor_free(enc);

  tt_assert(nodefamily_contains_rsa_id(nf1, d1));
  tt_assert(! nodefamily_contains_rsa_id(nf1, d2));
  tt_assert(nodefamily_contains_nickname(nf1, "hello"));
  tt_assert(nodefamily_contains_nickname(nf1, "HELLO"));
  tt_assert(! nodefamily_contains_nickname(nf1, "goodbye"));

  tt_int_op(nf1->refcnt, OP_EQ, 3);
  nodefamily_free(nf3);
  tt_int_op(nf1->refcnt, OP_EQ, 2);

  /* Try parsing with a provided self RSA digest. */
  nf3 = nodefamily_parse("hello ", d1, 0);
  tt_assert(nf3);
  tt_ptr_op(nf1, OP_EQ, nf3);

  /* Do we get the expected result when we re-encode? */
  tor_asprintf(&enc, "$%s hello", h1);
  enc2 = nodefamily_format(nf1);
  tt_str_op(enc2, OP_EQ, enc);
  tor_free(enc2);
  tor_free(enc);

  /* Make sure that we get a different result if we give a different digest. */
  nodefamily_free(nf3);
  tor_asprintf(&enc, "hello $%s hello", h3);
  nf3 = nodefamily_parse(enc, NULL, 0);
  tt_assert(nf3);
  tt_ptr_op(nf1, OP_NE, nf3);
  tor_free(enc);

  tt_assert(nodefamily_contains_rsa_id(nf3, d3));
  tt_assert(! nodefamily_contains_rsa_id(nf3, d2));
  tt_assert(! nodefamily_contains_rsa_id(nf3, d1));
  tt_assert(nodefamily_contains_nickname(nf3, "hello"));
  tt_assert(! nodefamily_contains_nickname(nf3, "goodbye"));

  nodefamily_free(nf1);
  nodefamily_free(nf2);
  nodefamily_free(nf3);

  /* Try one with several digests, all with nicknames appended, in different
     formats. */
  tor_asprintf(&enc, "%s $%s $%s=res $%s~ist", h1, h2, h3, h4);
  nf1 = nodefamily_parse(enc, d5, 0);
  tt_assert(nf1);
  tt_assert(nodefamily_contains_rsa_id(nf1, d1));
  tt_assert(nodefamily_contains_rsa_id(nf1, d2));
  tt_assert(nodefamily_contains_rsa_id(nf1, d3));
  tt_assert(nodefamily_contains_rsa_id(nf1, d4));
  tt_assert(nodefamily_contains_rsa_id(nf1, d5));
  /* Nicknames aren't preserved when ids are present, since node naming is
   * deprecated */
  tt_assert(! nodefamily_contains_nickname(nf3, "res"));
  tor_free(enc);
  tor_asprintf(&enc, "$%s $%s $%s $%s $%s", h4, h3, h1, h5, h2);
  enc2 = nodefamily_format(nf1);
  tt_str_op(enc, OP_EQ, enc2);
  tor_free(enc);
  tor_free(enc2);

  /* Try ones where we parse the empty string. */
  nf2 = nodefamily_parse("", NULL, 0);
  nf3 = nodefamily_parse("", d4, 0);
  tt_assert(nf2);
  tt_assert(nf3);
  tt_ptr_op(nf2, OP_NE, nf3);

  tt_assert(! nodefamily_contains_rsa_id(nf2, d4));
  tt_assert(nodefamily_contains_rsa_id(nf3, d4));
  tt_assert(! nodefamily_contains_rsa_id(nf2, d5));
  tt_assert(! nodefamily_contains_rsa_id(nf3, d5));
  tt_assert(! nodefamily_contains_nickname(nf2, "fred"));
  tt_assert(! nodefamily_contains_nickname(nf3, "bosco"));

  /* The NULL family should contain nothing. */
  tt_assert(! nodefamily_contains_rsa_id(NULL, d4));
  tt_assert(! nodefamily_contains_rsa_id(NULL, d5));

 done:
  tor_free(enc);
  tor_free(enc2);
  nodefamily_free(nf1);
  nodefamily_free(nf2);
  nodefamily_free(nf3);
  nodefamily_free_all();
}

static void
test_nodelist_nodefamily_parse_err(void *arg)
{
  (void)arg;
  nodefamily_t *nf1 = NULL;
  char *enc = NULL;
  const char *semibogus =
    "sdakljfdslkfjdsaklfjdkl9sdf " // too long for nickname
    "$jkASDFLkjsadfjhkl " // not hex
    "$7468696e67732d696e2d7468656d73656c766573 " // ok
    "reticulatogranulate "// ok
    "$73656d69616e7468726f706f6c6f676963616c6c79 " // too long for hex
    "$616273656e746d696e6465646e6573736573" // too short for hex
    ;

  setup_capture_of_logs(LOG_WARN);

  // We only get two items when we parse this.
  for (int reject = 0; reject <= 1; ++reject) {
    for (int log_at_warn = 0; log_at_warn <= 1; ++log_at_warn) {
      unsigned flags = log_at_warn ? NF_WARN_MALFORMED : 0;
      flags |= reject ? NF_REJECT_MALFORMED : 0;
      nf1 = nodefamily_parse(semibogus, NULL, flags);
      if (reject) {
        tt_assert(nf1 == NULL);
      } else {
        tt_assert(nf1);
        enc = nodefamily_format(nf1);
        tt_str_op(enc, OP_EQ,
                  "$7468696E67732D696E2D7468656D73656C766573 "
                  "reticulatogranulate");
        tor_free(enc);
      }

      if (log_at_warn) {
        expect_log_msg_containing("$616273656e746d696e6465646e6573736573");
        expect_log_msg_containing("sdakljfdslkfjdsaklfjdkl9sdf");
      } else {
        tt_int_op(mock_saved_log_n_entries(), OP_EQ, 0);
      }
      mock_clean_saved_logs();
    }
  }

 done:
  tor_free(enc);
  nodefamily_free(nf1);
  teardown_capture_of_logs();
}

static const node_t *
mock_node_get_by_id(const char *id)
{
  if (fast_memeq(id, "!!!!!!!!!!!!!!!!!!!!", DIGEST_LEN))
    return NULL;

  // use tor_free, not node_free.
  node_t *fake_node = tor_malloc_zero(sizeof(node_t));
  memcpy(fake_node->identity, id, DIGEST_LEN);
  return fake_node;
}

static const node_t *
mock_node_get_by_nickname(const char *nn, unsigned flags)
{
  (void)flags;
  if (!strcmp(nn, "nonesuch"))
    return NULL;

  // use tor_free, not node_free.
  node_t *fake_node = tor_malloc_zero(sizeof(node_t));
  strlcpy(fake_node->identity, nn, DIGEST_LEN);
  return fake_node;
}

static void
test_nodelist_nodefamily_lookup(void *arg)
{
  (void)arg;
  MOCK(node_get_by_nickname, mock_node_get_by_nickname);
  MOCK(node_get_by_id, mock_node_get_by_id);
  smartlist_t *sl = smartlist_new();
  nodefamily_t *nf1 = NULL;
  char *mem_op_hex_tmp = NULL;

  // 'null' is allowed.
  nodefamily_add_nodes_to_smartlist(NULL, sl);
  tt_int_op(smartlist_len(sl), OP_EQ, 0);

  // Try a real family
  nf1 = nodefamily_parse("$EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE "
                         "$2121212121212121212121212121212121212121 "
                         "$3333333333333333333333333333333333333333 "
                         "erewhon nonesuch", NULL, 0);
  tt_assert(nf1);
  nodefamily_add_nodes_to_smartlist(nf1, sl);
  // There were 5 elements; 2 were dropped because the mocked lookup failed.
  tt_int_op(smartlist_len(sl), OP_EQ, 3);

  const node_t *n = smartlist_get(sl, 0);
  test_memeq_hex(n->identity, "3333333333333333333333333333333333333333");
  n = smartlist_get(sl, 1);
  test_memeq_hex(n->identity, "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE");
  n = smartlist_get(sl, 2);
  tt_str_op(n->identity, OP_EQ, "erewhon");

 done:
  UNMOCK(node_get_by_nickname);
  UNMOCK(node_get_by_id);
  SMARTLIST_FOREACH(sl, node_t *, fake_node, tor_free(fake_node));
  smartlist_free(sl);
  nodefamily_free(nf1);
  tor_free(mem_op_hex_tmp);
}

static void
test_nodelist_nickname_matches(void *arg)
{
  (void)arg;
  node_t mock_node;
  routerstatus_t mock_rs;
  memset(&mock_node, 0, sizeof(mock_node));
  memset(&mock_rs, 0, sizeof(mock_rs));

  strlcpy(mock_rs.nickname, "evilgeniuses", sizeof(mock_rs.nickname));
  mock_node.rs = &mock_rs;
  memcpy(mock_node.identity, ".forabettertomorrow.", DIGEST_LEN);

#define match(x) tt_assert(node_nickname_matches(&mock_node, (x)))
#define no_match(x) tt_assert(! node_nickname_matches(&mock_node, (x)))

  match("evilgeniuses");
  match("EvilGeniuses");
  match("EvilGeniuses");
  match("2e666f7261626574746572746f6d6f72726f772e");
  match("2E666F7261626574746572746F6D6F72726F772E");
  match("$2e666f7261626574746572746f6d6f72726f772e");
  match("$2E666F7261626574746572746F6D6F72726F772E");
  match("$2E666F7261626574746572746F6D6F72726F772E~evilgeniuses");
  match("$2E666F7261626574746572746F6D6F72726F772E~EVILGENIUSES");

  no_match("evilgenius");
  no_match("evilgeniuseses");
  no_match("evil.genius");
  no_match("$2E666F7261626574746572746F6D6F72726FFFFF");
  no_match("2E666F7261626574746572746F6D6F72726FFFFF");
  no_match("$2E666F7261626574746572746F6D6F72726F772E~fred");
  no_match("$2E666F7261626574746572746F6D6F72726F772E=EVILGENIUSES");
 done:
  ;
}

static void
test_nodelist_node_nodefamily(void *arg)
{
  (void)arg;
  node_t mock_node1;
  routerstatus_t mock_rs;
  microdesc_t mock_md;

  node_t mock_node2;
  routerinfo_t mock_ri;

  smartlist_t *nodes=smartlist_new();

  memset(&mock_node1, 0, sizeof(mock_node1));
  memset(&mock_node2, 0, sizeof(mock_node2));
  memset(&mock_rs, 0, sizeof(mock_rs));
  memset(&mock_md, 0, sizeof(mock_md));
  memset(&mock_ri, 0, sizeof(mock_ri));

  mock_node1.rs = &mock_rs;
  mock_node1.md = &mock_md;

  mock_node2.ri = &mock_ri;

  strlcpy(mock_rs.nickname, "nodeone", sizeof(mock_rs.nickname));
  mock_ri.nickname = tor_strdup("nodetwo");

  memcpy(mock_node1.identity, "NodeOneNode1NodeOne1", DIGEST_LEN);
  memcpy(mock_node2.identity, "SecondNodeWe'reTestn", DIGEST_LEN);

  // empty families.
  tt_assert(! node_family_contains(&mock_node1, &mock_node2));
  tt_assert(! node_family_contains(&mock_node2, &mock_node1));

  // Families contain nodes, but not these nodes
  mock_ri.declared_family = smartlist_new();
  smartlist_add(mock_ri.declared_family, (char*)"NodeThree");
  mock_md.family = nodefamily_parse("NodeFour", NULL, 0);
  tt_assert(! node_family_contains(&mock_node1, &mock_node2));
  tt_assert(! node_family_contains(&mock_node2, &mock_node1));

  // Families contain one another.
  smartlist_add(mock_ri.declared_family, (char*)
                "4e6f64654f6e654e6f6465314e6f64654f6e6531");
  tt_assert(! node_family_contains(&mock_node1, &mock_node2));
  tt_assert(node_family_contains(&mock_node2, &mock_node1));

  nodefamily_free(mock_md.family);
  mock_md.family = nodefamily_parse(
            "NodeFour "
            "5365636f6e644e6f64655765277265546573746e", NULL, 0);
  tt_assert(node_family_contains(&mock_node1, &mock_node2));
  tt_assert(node_family_contains(&mock_node2, &mock_node1));

  // Try looking up families now.
  MOCK(node_get_by_nickname, mock_node_get_by_nickname);
  MOCK(node_get_by_id, mock_node_get_by_id);

  node_lookup_declared_family(nodes, &mock_node1);
  tt_int_op(smartlist_len(nodes), OP_EQ, 2);
  const node_t *n = smartlist_get(nodes, 0);
  tt_mem_op(n->identity, OP_EQ, "SecondNodeWe'reTestn", DIGEST_LEN);
  n = smartlist_get(nodes, 1);
  tt_str_op(n->identity, OP_EQ, "nodefour");

  // free, try the other one.
  SMARTLIST_FOREACH(nodes, node_t *, x, tor_free(x));
  smartlist_clear(nodes);

  node_lookup_declared_family(nodes, &mock_node2);
  tt_int_op(smartlist_len(nodes), OP_EQ, 2);
  n = smartlist_get(nodes, 0);
  // This gets a truncated hex hex ID since it was looked up by name
  tt_str_op(n->identity, OP_EQ, "NodeThree");
  n = smartlist_get(nodes, 1);
  tt_str_op(n->identity, OP_EQ, "4e6f64654f6e654e6f6");

 done:
  UNMOCK(node_get_by_nickname);
  UNMOCK(node_get_by_id);
  smartlist_free(mock_ri.declared_family);
  nodefamily_free(mock_md.family);
  tor_free(mock_ri.nickname);
  // use tor_free, these aren't real nodes
  SMARTLIST_FOREACH(nodes, node_t *, x, tor_free(x));
  smartlist_free(nodes);
}

static void
test_nodelist_nodefamily_canonicalize(void *arg)
{
  (void)arg;
  char *c = NULL;

  c = nodefamily_canonicalize("", NULL, 0);
  tt_str_op(c, OP_EQ, "");
  tor_free(c);

  uint8_t own_id[20];
  memset(own_id, 0, sizeof(own_id));
  c = nodefamily_canonicalize(
           "alice BOB caroL %potrzebie !!!@#@# "
           "$bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb=fred "
           "ffffffffffffffffffffffffffffffffffffffff "
           "$cccccccccccccccccccccccccccccccccccccccc ", own_id, 0);
  tt_str_op(c, OP_EQ,
           "!!!@#@# "
           "$0000000000000000000000000000000000000000 "
           "$BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB "
           "$CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC "
           "$FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF "
           "%potrzebie "
           "alice bob carol");

 done:
  tor_free(c);
}

#define NODE(name, flags) \
  { #name, test_nodelist_##name, (flags), NULL, NULL }

struct testcase_t nodelist_tests[] = {
  NODE(node_get_verbose_nickname_by_id_null_node, TT_FORK),
  NODE(node_get_verbose_nickname_not_named, TT_FORK),
  NODE(node_is_dir, TT_FORK),
  NODE(ed_id, TT_FORK),
  NODE(nodefamily, TT_FORK),
  NODE(nodefamily_parse_err, TT_FORK),
  NODE(nodefamily_lookup, TT_FORK),
  NODE(nickname_matches, 0),
  NODE(node_nodefamily, TT_FORK),
  NODE(nodefamily_canonicalize, 0),
  END_OF_TESTCASES
};
