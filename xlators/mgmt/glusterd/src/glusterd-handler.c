/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>


#include "globals.h"
#include "glusterfs.h"
#include "compat.h"
#include "dict.h"
#include "protocol-common.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include "statedump.h"
#include "run.h"
#include "glusterd-mem-types.h"
#include "glusterd.h"
#include "glusterd-sm.h"
#include "glusterd-op-sm.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"

#include "glusterd1-xdr.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "rpc-clnt.h"
#include "glusterd-volgen.h"
#include "glusterd-mountbroker.h"

#include <sys/resource.h>
#include <inttypes.h>

#include "defaults.c"
#include "common-utils.h"

static int
glusterd_handle_friend_req (rpcsvc_request_t *req, uuid_t  uuid,
                            char *hostname, int port,
                            gd1_mgmt_friend_req *friend_req)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;
        char                            rhost[UNIX_PATH_MAX + 1] = {0};
        uuid_t                          friend_uuid = {0};
        dict_t                          *dict = NULL;

        uuid_parse (uuid_utoa (uuid), friend_uuid);
        if (!port)
                port = GF_DEFAULT_BASE_PORT;

        ret = glusterd_remote_hostname_get (req, rhost, sizeof (rhost));
        ret = glusterd_friend_find (uuid, rhost, &peerinfo);

        if (ret) {
                ret = glusterd_xfer_friend_add_resp (req, rhost, port, -1,
                                                     GF_PROBE_UNKNOWN_PEER);
                if (friend_req->vols.vols_val)
                        free (friend_req->vols.vols_val);
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_FRIEND_REQ, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "event generation failed: %d", ret);
                return ret;
        }

        event->peerinfo = peerinfo;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (friend_req->vols.vols_val,
                                friend_req->vols.vols_len,
                                &dict);

        if (ret)
                goto out;
        else
                dict->extra_stdfree = friend_req->vols.vols_val;

        ctx->vols = dict;
        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                if (ctx)
                        GF_FREE (ctx);
                if (dict) {
                        if ((!dict->extra_stdfree) &&
                            friend_req->vols.vols_val)
                                free (friend_req->vols.vols_val);
                        dict_unref (dict);
                } else {
                    if (friend_req->vols.vols_val)
                        free (friend_req->vols.vols_val);
                }
                if (event)
                        GF_FREE (event);
        } else {
                if (peerinfo && (0 == peerinfo->connected))
                        ret = GLUSTERD_CONNECTION_AWAITED;
        }
        return ret;
}

static int
glusterd_handle_unfriend_req (rpcsvc_request_t *req, uuid_t  uuid,
                              char *hostname, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_friend_req_ctx_t       *ctx = NULL;

        if (!port)
                port = GF_DEFAULT_BASE_PORT;

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_CRITICAL,
                        "Received remove-friend from unknown peer %s",
                        hostname);
                ret = glusterd_xfer_friend_remove_resp (req, hostname,
                                                        port);
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                        (GD_FRIEND_EVENT_RCVD_REMOVE_FRIEND, &event);

        if (ret) {
                gf_log ("", GF_LOG_ERROR, "event generation failed: %d", ret);
                return ret;
        }

        event->peerinfo = peerinfo;

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_friend_req_ctx_t);

        if (!ctx) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                ret = -1;
                goto out;
        }

        uuid_copy (ctx->uuid, uuid);
        if (hostname)
                ctx->hostname = gf_strdup (hostname);
        ctx->req = req;

        event->ctx = ctx;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

        ret = 0;

out:
        if (0 != ret) {
                if (ctx && ctx->hostname)
                        GF_FREE (ctx->hostname);
                if (ctx)
                        GF_FREE (ctx);
        }

        return ret;
}

static int
glusterd_add_peer_detail_to_dict (glusterd_peerinfo_t   *peerinfo,
                                  dict_t  *friends, int   count)
{

        int             ret = -1;
        char            key[256] = {0, };

        GF_ASSERT (peerinfo);
        GF_ASSERT (friends);

        snprintf (key, 256, "friend%d.uuid", count);
        uuid_utoa_r (peerinfo->uuid, peerinfo->uuid_str);
        ret = dict_set_str (friends, key, peerinfo->uuid_str);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.hostname", count);
        ret = dict_set_str (friends, key, peerinfo->hostname);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.port", count);
        ret = dict_set_int32 (friends, key, peerinfo->port);
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.state", count);
        ret = dict_set_str (friends, key,
                    glusterd_friend_sm_state_name_get(peerinfo->state.state));
        if (ret)
                goto out;

        snprintf (key, 256, "friend%d.connected", count);
        ret = dict_set_int32 (friends, key, (int32_t)peerinfo->connected);
        if (ret)
                goto out;

out:
        return ret;
}


int
glusterd_add_volume_detail_to_dict (glusterd_volinfo_t *volinfo,
                                    dict_t  *volumes, int   count)
{

        int                     ret = -1;
        char                    key[256] = {0, };
        glusterd_brickinfo_t    *brickinfo = NULL;
        char                    *buf = NULL;
        int                     i = 1;
        data_pair_t             *pairs = NULL;
        char                    reconfig_key[256] = {0, };
        dict_t                  *dict = NULL;
        data_t                  *value = NULL;
        int                     opt_count = 0;
        glusterd_conf_t         *priv = NULL;
        char                    *volume_id_str  = NULL;


        GF_ASSERT (volinfo);
        GF_ASSERT (volumes);

        priv = THIS->private;

        GF_ASSERT (priv);

        snprintf (key, 256, "volume%d.name", count);
        ret = dict_set_str (volumes, key, volinfo->volname);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.type", count);
        ret = dict_set_int32 (volumes, key, volinfo->type);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.status", count);
        ret = dict_set_int32 (volumes, key, volinfo->status);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.brick_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->brick_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.dist_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->dist_leaf_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.stripe_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->stripe_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.replica_count", count);
        ret = dict_set_int32 (volumes, key, volinfo->replica_count);
        if (ret)
                goto out;

        snprintf (key, 256, "volume%d.transport", count);
        ret = dict_set_int32 (volumes, key, volinfo->transport_type);
        if (ret)
                goto out;

        volume_id_str = gf_strdup (uuid_utoa (volinfo->volume_id));
        if (!volume_id_str)
                goto out;

        snprintf (key, sizeof (key), "volume%d.volume_id", count);
        ret = dict_set_dynstr (volumes, key, volume_id_str);
        if (ret)
                goto out;

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                char    brick[1024] = {0,};
                snprintf (key, 256, "volume%d.brick%d", count, i);
                snprintf (brick, 1024, "%s:%s", brickinfo->hostname,
                          brickinfo->path);
                buf = gf_strdup (brick);
                ret = dict_set_dynstr (volumes, key, buf);
                if (ret)
                        goto out;
                i++;
        }

        dict = volinfo->dict;
        if (!dict) {
                ret = 0;
                goto out;
        }

        pairs = dict->members_list;

        while (pairs) {
                if (1 == glusterd_check_option_exists (pairs->key, NULL)) {
                        value = pairs->value;
                        if (!value)
                                continue;

                        snprintf (reconfig_key, 256, "volume%d.option.%s", count,
                                  pairs->key);
                        ret = dict_set_str  (volumes, reconfig_key, value->data);
                        if (!ret)
                            opt_count++;
                }
                pairs = pairs->next;
        }

        snprintf (key, 256, "volume%d.opt_count", count);
        ret = dict_set_int32 (volumes, key, opt_count);
out:
        return ret;
}

int
glusterd_friend_find (uuid_t uuid, char *hostname,
                      glusterd_peerinfo_t **peerinfo)
{
        int     ret = -1;

        if (uuid) {
                ret = glusterd_friend_find_by_uuid (uuid, peerinfo);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_INFO,
                                 "Unable to find peer by uuid");
                } else {
                        goto out;
                }

        }

        if (hostname) {
                ret = glusterd_friend_find_by_hostname (hostname, peerinfo);

                if (ret) {
                        gf_log ("glusterd", GF_LOG_INFO,
                                "Unable to find hostname: %s", hostname);
                } else {
                        goto out;
                }
        }

out:
        return ret;
}

int32_t
glusterd_op_txn_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx)
{
        int32_t                  ret    = -1;
        xlator_t                *this   = NULL;
        glusterd_conf_t         *priv   = NULL;
        int32_t                  locked = 0;

        GF_ASSERT (req);
        GF_ASSERT ((op > GD_OP_NONE) && (op < GD_OP_MAX));
        GF_ASSERT (NULL != ctx);

        this = THIS;
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_lock (priv->uuid);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to acquire local lock, ret: %d", ret);
                goto out;
        }

        locked = 1;
        gf_log (this->name, GF_LOG_INFO, "Acquired local lock");

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_START_LOCK, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to acquire cluster"
                        " lock.");
                goto out;
        }

        glusterd_op_set_op (op);
        glusterd_op_set_ctx (ctx);
        glusterd_op_set_req (req);


out:
        if (locked && ret)
                glusterd_unlock (priv->uuid);

        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_cluster_lock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_lock_req       lock_req = {{0},};
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &lock_req, (xdrproc_t)xdr_gd1_mgmt_cluster_lock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO,
                "Received LOCK from uuid: %s", uuid_utoa (lock_req.uuid));

        if (glusterd_friend_find_by_uuid (lock_req.uuid, &peerinfo)) {
                gf_log (THIS->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (lock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }

        uuid_copy (ctx->uuid, lock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCK, ctx);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_req_ctx_create (rpcsvc_request_t *rpc_req,
                         glusterd_op_t op, uuid_t uuid,
                         char *buf_val, size_t buf_len,
                         gf_gld_mem_types_t mem_type,
                         glusterd_req_ctx_t **req_ctx_out)
{
        int                 ret     = -1;
        char                str[50] = {0,};
        glusterd_req_ctx_t *req_ctx = NULL;
        dict_t             *dict    = NULL;

        uuid_unparse (uuid, str);
        gf_log ("glusterd", GF_LOG_INFO,
                "Received op from uuid: %s", str);

        dict = dict_new ();
        if (!dict)
                goto out;

        req_ctx = GF_CALLOC (1, sizeof (*req_ctx), mem_type);
        if (!req_ctx) {
                goto out;
        }

        uuid_copy (req_ctx->uuid, uuid);
        req_ctx->op = op;
        ret = dict_unserialize (buf_val, buf_len, &dict);
        if (ret) {
                gf_log ("", GF_LOG_WARNING,
                        "failed to unserialize the dictionary");
                goto out;
        }

        req_ctx->dict = dict;
        req_ctx->req = rpc_req;
        *req_ctx_out = req_ctx;
        ret = 0;
out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
                if (req_ctx)
                        GF_FREE (req_ctx);
        }
        return ret;
}

int
glusterd_handle_stage_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        glusterd_req_ctx_t              *req_ctx = NULL;
        gd1_mgmt_stage_op_req           op_req = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);
        if (!xdr_to_generic (req->msg[0], &op_req, (xdrproc_t)xdr_gd1_mgmt_stage_op_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_friend_find_by_uuid (op_req.uuid, &peerinfo)) {
                gf_log (THIS->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        ret = glusterd_req_ctx_create (req, op_req.op, op_req.uuid,
                                       op_req.buf.buf_val, op_req.buf.buf_len,
                                       gf_gld_mt_op_stage_ctx_t, &req_ctx);
        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_STAGE_OP, req_ctx);

 out:
        if (op_req.buf.buf_val)
                free (op_req.buf.buf_val);//malloced by xdr
        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}

int
glusterd_handle_commit_op (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        glusterd_req_ctx_t              *req_ctx = NULL;
        gd1_mgmt_commit_op_req          op_req = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &op_req, (xdrproc_t)xdr_gd1_mgmt_commit_op_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (glusterd_friend_find_by_uuid (op_req.uuid, &peerinfo)) {
                gf_log (THIS->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (op_req.uuid));
                ret = -1;
                goto out;
        }

        //the structures should always be equal
        GF_ASSERT (sizeof (gd1_mgmt_commit_op_req) == sizeof (gd1_mgmt_stage_op_req));
        ret = glusterd_req_ctx_create (req, op_req.op, op_req.uuid,
                                       op_req.buf.buf_val, op_req.buf.buf_len,
                                       gf_gld_mt_op_commit_ctx_t, &req_ctx);
        if (ret)
                goto out;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_COMMIT_OP, req_ctx);
        if (ret)
                goto out;
        ret = glusterd_op_init_ctx (op_req.op);

out:
        if (op_req.buf.buf_val)
                free (op_req.buf.buf_val);//malloced by xdr
        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}
int
glusterd_handle_cli_probe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_probe_req               cli_req = {0,};
        glusterd_peerinfo_t             *peerinfo = NULL;
        gf_boolean_t                    run_fsm = _gf_true;
        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf1_cli_probe_req)) {
                //failed to decode msg;
                gf_log ("", GF_LOG_ERROR, "xdr decoding error");
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_cmd_log ("peer probe", " on host %s:%d", cli_req.hostname,
                    cli_req.port);
        gf_log ("glusterd", GF_LOG_INFO, "Received CLI probe req %s %d",
                cli_req.hostname, cli_req.port);

        if (!(ret = glusterd_is_local_addr(cli_req.hostname))) {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_LOCALHOST,
                                              cli_req.hostname, cli_req.port);
                goto out;
        }

        if (!(ret = glusterd_friend_find_by_hostname(cli_req.hostname,
                                         &peerinfo))) {
                if (strcmp (peerinfo->hostname, cli_req.hostname) == 0) {

                        gf_log ("glusterd", GF_LOG_DEBUG, "Probe host %s port %d"
                               " already a peer", cli_req.hostname, cli_req.port);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND,
                                                      cli_req.hostname, cli_req.port);
                        goto out;
                }
        }
        ret = glusterd_probe_begin (req, cli_req.hostname, cli_req.port);

        gf_cmd_log ("peer probe","on host %s:%d %s",cli_req.hostname, cli_req.port,
                    (ret) ? "FAILED" : "SUCCESS");

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;
        }
out:
        if (cli_req.hostname)
                free (cli_req.hostname);//its malloced by xdr

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_cli_deprobe (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_deprobe_req               cli_req = {0,};
        uuid_t                          uuid = {0};
        int                             op_errno = 0;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *priv = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
            (xdrproc_t)xdr_gf1_cli_deprobe_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received CLI deprobe req");

        ret = glusterd_hostname_to_uuid (cli_req.hostname, uuid);
        if (ret) {
                op_errno = GF_DEPROBE_NOT_FRIEND;
                goto out;
        }

        if (!uuid_compare (uuid, priv->uuid)) {
                op_errno = GF_DEPROBE_LOCALHOST;
                ret = -1;
                goto out;
        }

        if (!uuid_is_null (uuid) && !(cli_req.flags & GF_CLI_FLAG_OP_FORCE)) {
                /* Check if peers are connected, except peer being detached*/
                if (!glusterd_chk_peers_connected_befriended (uuid)) {
                        ret = -1;
                        op_errno = GF_DEPROBE_FRIEND_DOWN;
                        goto out;
                }
                ret = glusterd_all_volume_cond_check (
                                                glusterd_friend_brick_belongs,
                                                -1, &uuid);
                if (ret) {
                        op_errno = GF_DEPROBE_BRICK_EXIST;
                        goto out;
                }
        }

        if (!uuid_is_null (uuid)) {
                ret = glusterd_deprobe_begin (req, cli_req.hostname,
                                              cli_req.port, uuid);
        } else {
                ret = glusterd_deprobe_begin (req, cli_req.hostname,
                                              cli_req.port, NULL);
        }

        gf_cmd_log ("peer deprobe", "on host %s:%d %s", cli_req.hostname,
                    cli_req.port, (ret) ? "FAILED" : "SUCCESS");
out:
        if (ret) {
                ret = glusterd_xfer_cli_deprobe_resp (req, ret, op_errno,
                                                      cli_req.hostname);
        }

        if (cli_req.hostname)
                free (cli_req.hostname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_list_friends (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_peer_list_req           cli_req = {0,};
        dict_t                          *dict = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf1_cli_peer_list_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received cli list req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = glusterd_list_friends (req, dict, cli_req.flags);

out:
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_get_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        int32_t                         flags = 0;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received get vol req");

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get flags");
                goto out;
        }

        ret = glusterd_get_volumes (req, dict, flags);

out:
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int32_t
glusterd_op_begin (rpcsvc_request_t *req, glusterd_op_t op, void *ctx)
{
        int             ret = -1;

        ret = glusterd_op_txn_begin (req, op, ctx);

        return ret;
}



int
glusterd_handle_reset_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_RESET_VOLUME;
        char                            *volname = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR, "failed to "
                                    "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get volname");
                goto out;
        }

        gf_cmd_log ("Volume reset", "volume  : %s", volname);
        ret = glusterd_op_begin (req, GD_OP_RESET_VOLUME, dict);
        gf_cmd_log ("Volume reset", " on volume %s %s ", volname,
                ((ret == 0)? " SUCCEDED":" FAILED"));

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();
        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");
        }

        return ret;
}


int
glusterd_handle_set_volume (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_SET_VOLUME;
        char                            *key = NULL;
        char                            *value = NULL;
        char                            *volname = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "Unable to get volume name, while"
                        "handling volume set command");
                goto out;
        }

        ret = dict_get_str (dict, "key1", &key);
        if (ret) {
                if (strcmp (volname, "help-xml") && strcmp (volname, "help")) {
                        gf_log ("", GF_LOG_WARNING, "Unable to get key, while "
                                "handling volume set for %s",volname);
                        goto out;
                }
        }

        ret = dict_get_str (dict, "value1", &value);
        if (ret) {
                if (strcmp (volname, "help-xml") && strcmp (volname, "help")) {
                        gf_log ("", GF_LOG_WARNING, "Unable to get value, while"
                                "handling volume set for %s",volname);
                        goto out;
                }
        }


        gf_cmd_log ("volume set", "volume-name:%s: key:%s, value:%s",volname,
                    key, value);
        ret = glusterd_op_begin (req, GD_OP_SET_VOLUME, dict);
        gf_cmd_log ("volume set", "volume-name:%s: key:%s, value:%s %s",
                    volname, key, value, (ret == 0)? "SUCCEDED" : "FAILED" );
out:

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");
        }
        return ret;
}

int
glusterd_handle_sync_volume (rpcsvc_request_t *req)
{
        int32_t                          ret     = -1;
        gf_cli_req                       cli_req = {{0,}};
        dict_t                           *dict = NULL;
        gf_cli_rsp                       cli_rsp = {0.};
        char                             msg[2048] = {0,};
        glusterd_volinfo_t               *volinfo = NULL;
        char                             *volname = NULL;
        gf1_cli_sync_volume              flags = 0;
        char                             *hostname = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get hostname");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                ret = dict_get_int32 (dict, "flags", (int32_t*)&flags);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR, "Unable to get volume"
                                "name, or flags");
                        goto out;
                }
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received volume sync req "
                "for volume %s",
                (flags & GF_CLI_SYNC_ALL) ? "all" : volname);

        if (!glusterd_is_local_addr (hostname)) {
                ret = -1;
                snprintf (msg, sizeof (msg), "sync from localhost"
                          " not allowed");
                goto out;
        }

        if (!flags) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (!ret) {
                        snprintf (msg, sizeof (msg), "please delete the "
                                 "volume: %s before sync", volname);
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynmstr (dict, "volname", volname);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "volume name set failed");
                        snprintf (msg, sizeof (msg), "volume name set failed");
                        goto out;
                }
        } else {
                if (glusterd_volume_count_get ()) {
                        snprintf (msg, sizeof (msg), "please delete all the "
                                 "volumes before full sync");
                        ret = -1;
                        goto out;
                }
        }

        ret = glusterd_op_begin (req, GD_OP_SYNC_VOLUME, dict);

out:
        if (ret) {
                cli_rsp.op_ret = -1;
                cli_rsp.op_errstr = msg;
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                glusterd_submit_reply(req, &cli_rsp, NULL, 0, NULL,
                                      (xdrproc_t)xdr_gf_cli_rsp);
                if (dict)
                        dict_unref (dict);


                ret = 0; //sent error to cli, prevent second reply
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_fsm_log_send_resp (rpcsvc_request_t *req, int op_ret,
                            char *op_errstr, dict_t *dict)
{

        int                             ret = -1;
        gf1_cli_fsm_log_rsp             rsp = {0};

        GF_ASSERT (req);
        GF_ASSERT (op_errstr);

        rsp.op_ret = op_ret;
        rsp.op_errstr = op_errstr;
        if (rsp.op_ret == 0)
                ret = dict_allocate_and_serialize (dict, &rsp.fsm_log.fsm_log_val,
                                                (size_t *)&rsp.fsm_log.fsm_log_len);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
        if (rsp.fsm_log.fsm_log_val)
                GF_FREE (rsp.fsm_log.fsm_log_val);

        gf_log ("glusterd", GF_LOG_DEBUG, "Responded, ret: %d", ret);

        return 0;
}

int
glusterd_handle_fsm_log (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf1_cli_fsm_log_req             cli_req = {0,};
        dict_t                          *dict = NULL;
        glusterd_sm_tr_log_t            *log = NULL;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;
        char                            msg[2048] = {0};
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf1_cli_fsm_log_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                snprintf (msg, sizeof (msg), "Garbage request");
                goto out;
        }

        if (strcmp ("", cli_req.name) == 0) {
                this = THIS;
                conf = this->private;
                log = &conf->op_sm_log;
        } else {
                ret = glusterd_friend_find_by_hostname (cli_req.name,
                                                        &peerinfo);
                if (ret) {
                        snprintf (msg, sizeof (msg), "%s is not a peer",
                                  cli_req.name);
                        goto out;
                }
                log = &peerinfo->sm_log;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = glusterd_sm_tr_log_add_to_dict (dict, log);
out:
        (void)glusterd_fsm_log_send_resp (req, ret, msg, dict);
        if (cli_req.name)
                free (cli_req.name);//malloced by xdr
        if (dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return 0;//send 0 to avoid double reply
}

int
glusterd_op_lock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_lock_rsp       rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        glusterd_get_uuid (&rsp.uuid);
        rsp.op_ret = status;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_cluster_lock_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded, ret: %d", ret);

        return 0;
}

int
glusterd_op_unlock_send_resp (rpcsvc_request_t *req, int32_t status)
{

        gd1_mgmt_cluster_unlock_rsp     rsp = {{0},};
        int                             ret = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to unlock, ret: %d", ret);

        return ret;
}

int
glusterd_handle_cluster_unlock (rpcsvc_request_t *req)
{
        gd1_mgmt_cluster_unlock_req     unlock_req = {{0}, };
        int32_t                         ret = -1;
        glusterd_op_lock_ctx_t          *ctx = NULL;
        glusterd_peerinfo_t             *peerinfo = NULL;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &unlock_req,
                             (xdrproc_t)xdr_gd1_mgmt_cluster_unlock_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }


        gf_log ("glusterd", GF_LOG_INFO,
                "Received UNLOCK from uuid: %s", uuid_utoa (unlock_req.uuid));

        if (glusterd_friend_find_by_uuid (unlock_req.uuid, &peerinfo)) {
                gf_log (THIS->name, GF_LOG_WARNING, "%s doesn't "
                        "belong to the cluster. Ignoring request.",
                        uuid_utoa (unlock_req.uuid));
                ret = -1;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_gld_mt_op_lock_ctx_t);

        if (!ctx) {
                //respond here
                return -1;
        }
        uuid_copy (ctx->uuid, unlock_req.uuid);
        ctx->req = req;

        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_UNLOCK, ctx);

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_op_stage_send_resp (rpcsvc_request_t   *req,
                             int32_t op, int32_t status,
                             char *op_errstr, dict_t *rsp_dict)
{
        gd1_mgmt_stage_op_rsp           rsp      = {{0},};
        int                             ret      = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;
        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (rsp_dict,
                                           &rsp.dict.dict_val,
                                           (size_t *)&rsp.dict.dict_len);
        if (ret < 0) {
                gf_log ("", GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                return ret;
        }

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_stage_op_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to stage, ret: %d", ret);
        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);

        return ret;
}

int
glusterd_op_commit_send_resp (rpcsvc_request_t *req,
                               int32_t op, int32_t status, char *op_errstr,
                               dict_t *rsp_dict)
{
        gd1_mgmt_commit_op_rsp          rsp      = {{0}, };
        int                             ret      = -1;

        GF_ASSERT (req);
        rsp.op_ret = status;
        glusterd_get_uuid (&rsp.uuid);
        rsp.op = op;

        if (op_errstr)
                rsp.op_errstr = op_errstr;
        else
                rsp.op_errstr = "";

        if (rsp_dict) {
                ret = dict_allocate_and_serialize (rsp_dict,
                                                   &rsp.dict.dict_val,
                                                   (size_t *)&rsp.dict.dict_len);
                if (ret < 0) {
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        goto out;
                }
        }


        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_commit_op_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to commit, ret: %d", ret);

out:
        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
glusterd_handle_incoming_friend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        gf_boolean_t            run_fsm = _gf_true;

        GF_ASSERT (req);
        if (!xdr_to_generic (req->msg[0], &friend_req, (xdrproc_t)xdr_gd1_mgmt_friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO,
                "Received probe from uuid: %s", uuid_utoa (friend_req.uuid));
        ret = glusterd_handle_friend_req (req, friend_req.uuid,
                                          friend_req.hostname, friend_req.port,
                                          &friend_req);

        if (ret == GLUSTERD_CONNECTION_AWAITED) {
                //fsm should be run after connection establishes
                run_fsm = _gf_false;
                ret = 0;
        }

out:
        if (friend_req.hostname)
                free (friend_req.hostname);//malloced by xdr

        if (run_fsm) {
                glusterd_friend_sm ();
                glusterd_op_sm ();
        }

        return ret;
}

int
glusterd_handle_incoming_unfriend_req (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_req     friend_req = {{0},};
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);
        if (!xdr_to_generic (req->msg[0], &friend_req, (xdrproc_t)xdr_gd1_mgmt_friend_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO,
                "Received unfriend from uuid: %s", uuid_utoa (friend_req.uuid));

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get the remote hostname");
                goto out;
        }
        ret = glusterd_handle_unfriend_req (req, friend_req.uuid,
                                            remote_hostname, friend_req.port);

out:
        if (friend_req.hostname)
                free (friend_req.hostname);//malloced by xdr
        if (friend_req.vols.vols_val)
                free (friend_req.vols.vols_val);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_friend_update_delete (dict_t *dict)
{
        char                    *hostname = NULL;
        int32_t                 ret = -1;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret)
                goto out;

        ret = glusterd_friend_remove (NULL, hostname);

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_friend_hostname_update (glusterd_peerinfo_t *peerinfo,
                                char *hostname,
                                gf_boolean_t store_update)
{
        char                    *new_hostname = NULL;
        int                     ret = 0;

        GF_ASSERT (peerinfo);
        GF_ASSERT (hostname);

        new_hostname = gf_strdup (hostname);
        if (!new_hostname) {
                ret = -1;
                goto out;
        }

        GF_FREE (peerinfo->hostname);
        peerinfo->hostname = new_hostname;
        if (store_update)
                ret = glusterd_store_peerinfo (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_friend_update (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gd1_mgmt_friend_update     friend_req = {{0},};
        glusterd_peerinfo_t     *peerinfo = NULL;
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;
        glusterd_peerinfo_t     *tmp = NULL;
        gd1_mgmt_friend_update_rsp rsp = {{0},};
        dict_t                  *dict = NULL;
        char                    key[100] = {0,};
        char                    *uuid_buf = NULL;
        char                    *hostname = NULL;
        int                     i = 1;
        int                     count = 0;
        uuid_t                  uuid = {0,};
        glusterd_peerctx_args_t args = {0};
        int32_t                 op = 0;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!xdr_to_generic (req->msg[0], &friend_req, (xdrproc_t)xdr_gd1_mgmt_friend_update)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        ret = glusterd_friend_find (friend_req.uuid, NULL, &tmp);
        if (ret) {
                gf_log ("", GF_LOG_CRITICAL, "Received friend update request "
                        "from unknown peer %s", uuid_utoa (friend_req.uuid));
                goto out;
        }
        gf_log ("glusterd", GF_LOG_INFO,
                "Received friend update from uuid: %s", uuid_utoa (friend_req.uuid));

        if (friend_req.friends.friends_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (friend_req.friends.friends_val,
                                        friend_req.friends.friends_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = friend_req.friends.friends_val;
                }
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "op", &op);
        if (ret)
                goto out;

        if (GD_FRIEND_UPDATE_DEL == op) {
                ret = glusterd_handle_friend_update_delete (dict);
                goto out;
        }

        args.mode = GD_MODE_ON;
        while ( i <= count) {
                snprintf (key, sizeof (key), "friend%d.uuid", i);
                ret = dict_get_str (dict, key, &uuid_buf);
                if (ret)
                        goto out;
                uuid_parse (uuid_buf, uuid);
                snprintf (key, sizeof (key), "friend%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;

                gf_log ("", GF_LOG_INFO, "Received uuid: %s, hostname:%s",
                                uuid_buf, hostname);

                if (!uuid_compare (uuid, priv->uuid)) {
                        gf_log ("", GF_LOG_INFO, "Received my uuid as Friend");
                        i++;
                        continue;
                }

                ret = glusterd_friend_find (uuid, hostname, &tmp);

                if (!ret) {
                        if (strcmp (hostname, tmp->hostname) != 0) {
                                glusterd_friend_hostname_update (tmp, hostname,
                                                                 _gf_true);
                        }
                        i++;
                        continue;
                }

                ret = glusterd_friend_add (hostname, friend_req.port,
                                           GD_FRIEND_STATE_BEFRIENDED,
                                           &uuid, NULL, &peerinfo, 0, &args);

                i++;
        }

out:
        uuid_copy (rsp.uuid, priv->uuid);
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_update_rsp);
        if (dict) {
                if (!dict->extra_stdfree && friend_req.friends.friends_val)
                        free (friend_req.friends.friends_val);//malloced by xdr
                dict_unref (dict);
        } else {
                if (friend_req.friends.friends_val)
                        free (friend_req.friends.friends_val);//malloced by xdr
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_probe_query (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        xlator_t                        *this = NULL;
        glusterd_conf_t                 *conf = NULL;
        gd1_mgmt_probe_req              probe_req = {{0},};
        gd1_mgmt_probe_rsp              rsp = {{0},};
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        int                             port = 0;
        char               remote_hostname[UNIX_PATH_MAX + 1] = {0,};

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &probe_req, (xdrproc_t)xdr_gd1_mgmt_probe_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        this = THIS;

        conf = this->private;
        if (probe_req.port)
                port = probe_req.port;
        else
                port = GF_DEFAULT_BASE_PORT;

        gf_log ("glusterd", GF_LOG_INFO,
                "Received probe from uuid: %s", uuid_utoa (probe_req.uuid));

        ret = glusterd_remote_hostname_get (req, remote_hostname,
                                            sizeof (remote_hostname));
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get the remote hostname");
                goto out;
        }
        ret = glusterd_friend_find (probe_req.uuid, remote_hostname, &peerinfo);
        if ((ret != 0 ) && (!list_empty (&conf->peers))) {
                rsp.op_ret = -1;
                rsp.op_errno = GF_PROBE_ANOTHER_CLUSTER;
        } else if (ret) {
                gf_log ("glusterd", GF_LOG_INFO, "Unable to find peerinfo"
                        " for host: %s (%d)", remote_hostname, port);
                args.mode = GD_MODE_ON;
                ret = glusterd_friend_add (remote_hostname, port,
                                           GD_FRIEND_STATE_PROBE_RCVD,
                                           NULL, NULL, &peerinfo, 0, &args);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to add peer %s",
                                remote_hostname);
                        rsp.op_errno = GF_PROBE_ADD_FAILED;
                }
        }

        uuid_copy (rsp.uuid, conf->uuid);

        rsp.hostname = probe_req.hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_probe_rsp);

        gf_log ("glusterd", GF_LOG_INFO, "Responded to %s, op_ret: %d, "
                "op_errno: %d, ret: %d", probe_req.hostname,
                rsp.op_ret, rsp.op_errno, ret);

out:
        if (probe_req.hostname)
                free (probe_req.hostname);//malloced by xdr

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_cli_profile_volume (rpcsvc_request_t *req)
{
        int32_t                         ret     = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                          *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_PROFILE_VOLUME;
        char                            *volname = NULL;
        int32_t                         op = 0;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }




        if (cli_req.dict.dict_len > 0) {
                dict = dict_new();
                if (!dict)
                        goto out;
                dict_unserialize (cli_req.dict.dict_val,
                                  cli_req.dict.dict_len, &dict);

        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get volname");
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received volume profile req "
                "for volume %s", volname);
        ret = dict_get_int32 (dict, "op", &op);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get op");
                goto out;
        }

        gf_cmd_log ("Volume stats", "volume  : %s, op: %d", volname, op);
        ret = glusterd_op_begin (req, cli_op, dict);
        gf_cmd_log ("Volume stats", " on volume %s, op: %d %s ",
                    volname, op,
                    ((ret == 0)? " SUCCEDED":" FAILED"));

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret && dict)
                dict_unref (dict);
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val);
        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");

        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_handle_getwd (rpcsvc_request_t *req)
{
        int32_t                 ret = -1;
        gf1_cli_getwd_rsp     rsp = {0,};
        glusterd_conf_t         *priv = NULL;

        GF_ASSERT (req);

        priv = THIS->private;
        GF_ASSERT (priv);

        gf_log ("glusterd", GF_LOG_INFO, "Received getwd req");

        rsp.wd = priv->workdir;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_getwd_rsp);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}


int
glusterd_handle_mount (rpcsvc_request_t *req)
{
        gf1_cli_mount_req mnt_req = {0,};
        gf1_cli_mount_rsp rsp     = {0,};
        dict_t *dict              = NULL;
        int ret                   = 0;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &mnt_req, (xdrproc_t)xdr_gf1_cli_mount_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                rsp.op_ret = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received mount req");

        if (mnt_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (mnt_req.dict.dict_val,
                                        mnt_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        rsp.op_ret = -1;
                        rsp.op_errno = -EINVAL;
                        goto out;
                } else {
                        dict->extra_stdfree = mnt_req.dict.dict_val;
                }
        }

        rsp.op_ret = glusterd_do_mount (mnt_req.label, dict,
                                        &rsp.path, &rsp.op_errno);

 out:
        if (!rsp.path)
                rsp.path = "";

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_mount_rsp);

        if (dict)
                dict_unref (dict);
        if (*rsp.path)
                GF_FREE (rsp.path);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_handle_umount (rpcsvc_request_t *req)
{
        gf1_cli_umount_req umnt_req = {0,};
        gf1_cli_umount_rsp rsp      = {0,};
        char *mountbroker_root      = NULL;
        char mntp[PATH_MAX]         = {0,};
        char *path                  = NULL;
        runner_t runner             = {0,};
        int ret                     = 0;
        xlator_t *this              = THIS;
        gf_boolean_t dir_ok         = _gf_false;
        char *pdir                  = NULL;
        char *t                     = NULL;

        GF_ASSERT (req);
        GF_ASSERT (this);

        if (!xdr_to_generic (req->msg[0], &umnt_req, (xdrproc_t)xdr_gf1_cli_umount_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                rsp.op_ret = -1;
                goto out;
        }

        gf_log ("glusterd", GF_LOG_INFO, "Received umount req");

        if (dict_get_str (this->options, "mountbroker-root",
                          &mountbroker_root) != 0) {
                rsp.op_errno = ENOENT;
                goto out;
        }

        /* check if it is allowed to umount path */
        path = gf_strdup (umnt_req.path);
        if (!path) {
                rsp.op_errno = ENOMEM;
                goto out;
        }
        dir_ok = _gf_false;
        pdir = dirname (path);
        t = strtail (pdir, mountbroker_root);
        if (t && *t == '/') {
                t = strtail(++t, MB_HIVE);
                if (t && !*t)
                        dir_ok = _gf_true;
        }
        GF_FREE (path);
        if (!dir_ok) {
                rsp.op_errno = EACCES;
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, "umount", umnt_req.path, NULL);
        if (umnt_req.lazy)
                runner_add_arg (&runner, "-l");
        rsp.op_ret = runner_run (&runner);
        if (rsp.op_ret == 0) {
                if (realpath (umnt_req.path, mntp))
                        rmdir (mntp);
                else {
                        rsp.op_ret = -1;
                        rsp.op_errno = errno;
                }
                if (unlink (umnt_req.path) != 0) {
                        rsp.op_ret = -1;
                        rsp.op_errno = errno;
                }
        }

 out:
        if (rsp.op_errno)
                rsp.op_ret = -1;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_umount_rsp);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        return ret;
}

int
glusterd_friend_remove (uuid_t uuid, char *hostname)
{
        int                           ret = 0;
        glusterd_peerinfo_t           *peerinfo = NULL;

        ret = glusterd_friend_find (uuid, hostname, &peerinfo);
        if (ret)
                goto out;

        ret = glusterd_friend_remove_cleanup_vols (peerinfo->uuid);
        if (ret)
                gf_log (THIS->name, GF_LOG_WARNING, "Volumes cleanup failed");
        ret = glusterd_friend_cleanup (peerinfo);
out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_rpc_create (struct rpc_clnt **rpc,
                     dict_t *options,
                     rpc_clnt_notify_t notify_fn,
                     void *notify_data)
{
        struct rpc_clnt         *new_rpc = NULL;
        int                     ret = -1;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (options);
        new_rpc = rpc_clnt_new (options, this->ctx, this->name);

        if (!new_rpc)
                goto out;

        ret = rpc_clnt_register_notify (new_rpc, notify_fn, notify_data);
        *rpc = new_rpc;
        if (ret)
                goto out;
        ret = rpc_clnt_start (new_rpc);
out:
        if (ret) {
                if (new_rpc) {
                        (void) rpc_clnt_unref (new_rpc);
                }
        }

        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_transport_keepalive_options_get (int *interval, int *time)
{
        int     ret = 0;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-interval",
                              interval);
        ret = dict_get_int32 (this->options,
                              "transport.socket.keepalive-time",
                              time);
        return 0;
}

int
glusterd_transport_inet_keepalive_options_build (dict_t **options,
                                                 const char *hostname, int port)
{
        dict_t  *dict = NULL;
        int32_t interval = -1;
        int32_t time     = -1;
        int     ret = 0;

        GF_ASSERT (options);
        GF_ASSERT (hostname);

        if (!port)
                port = GLUSTERD_DEFAULT_PORT;
        ret = rpc_transport_inet_options_build (&dict, hostname, port);
        if (ret)
                goto out;

        glusterd_transport_keepalive_options_get (&interval, &time);

        if ((interval > 0) || (time > 0))
                ret = rpc_transport_keepalive_options_set (dict, interval, time);
        *options = dict;
out:
        gf_log ("glusterd", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_friend_add (const char *hoststr, int port,
                     glusterd_friend_sm_state_t state,
                     uuid_t *uuid,
                     struct rpc_clnt    *rpc,
                     glusterd_peerinfo_t **friend,
                     gf_boolean_t restore,
                     glusterd_peerctx_args_t *args)
{
        int                    ret = 0;
        glusterd_conf_t        *conf = NULL;
        glusterd_peerinfo_t    *peerinfo = NULL;
        glusterd_peerctx_t     *peerctx = NULL;
        gf_boolean_t           is_allocated = _gf_false;
        dict_t                 *options = NULL;

        conf = THIS->private;
        GF_ASSERT (conf)
        GF_ASSERT (hoststr);

        peerctx = GF_CALLOC (1, sizeof (*peerctx), gf_gld_mt_peerctx_t);
        if (!peerctx) {
                ret = -1;
                goto out;
        }

        if (args)
                peerctx->args = *args;

        ret = glusterd_peerinfo_new (&peerinfo, state, uuid, hoststr);
        if (ret)
                goto out;
        peerctx->peerinfo = peerinfo;
        if (friend)
                *friend = peerinfo;

        if (!rpc) {
                ret = glusterd_transport_inet_keepalive_options_build (&options,
                                                                 hoststr, port);
                if (ret)
                        goto out;
                ret = glusterd_rpc_create (&rpc, options,
                                           glusterd_peer_rpc_notify,
                                           peerctx);
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR, "failed to create rpc for"
                                " peer %s", (char*)hoststr);
                        goto out;
                }
                is_allocated = _gf_true;
        }

        peerinfo->rpc = rpc;

        if (!restore)
                ret = glusterd_store_peerinfo (peerinfo);

        list_add_tail (&peerinfo->uuid_list, &conf->peers);

out:
        if (ret) {
                if (peerctx)
                        GF_FREE (peerctx);
                if (is_allocated && rpc) {
                        (void) rpc_clnt_unref (rpc);
                }
                if (peerinfo) {
                        peerinfo->rpc = NULL;
                        (void) glusterd_friend_cleanup (peerinfo);
                }
        }

        gf_log ("glusterd", GF_LOG_INFO, "connect returned %d", ret);
        return ret;
}

int
glusterd_probe_begin (rpcsvc_request_t *req, const char *hoststr, int port)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_peerctx_args_t         args = {0};
        glusterd_friend_sm_event_t      *event = NULL;

        GF_ASSERT (hoststr);

        ret = glusterd_friend_find (NULL, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_INFO, "Unable to find peerinfo"
                        " for host: %s (%d)", hoststr, port);
                args.mode = GD_MODE_ON;
                args.req  = req;
                ret = glusterd_friend_add ((char *)hoststr, port,
                                           GD_FRIEND_STATE_DEFAULT,
                                           NULL, NULL, &peerinfo, 0, &args);
                if ((!ret) && (!peerinfo->connected)) {
                        ret = GLUSTERD_CONNECTION_AWAITED;
                }

        } else if (peerinfo->connected &&
                   (GD_FRIEND_STATE_BEFRIENDED == peerinfo->state.state)) {
                ret = glusterd_friend_hostname_update (peerinfo, (char*)hoststr,
                                                       _gf_false);
                if (ret)
                        goto out;
                //this is just to rename so inject local acc for cluster update
                ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_LOCAL_ACC,
                                                    &event);
                if (!ret) {
                        event->peerinfo = peerinfo;
                        ret = glusterd_friend_sm_inject_event (event);
                        glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_SUCCESS,
                                                      (char*)hoststr, port);
                }
        } else {
                glusterd_xfer_cli_probe_resp (req, 0, GF_PROBE_FRIEND,
                                              (char*)hoststr, port);
        }

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
glusterd_deprobe_begin (rpcsvc_request_t *req, const char *hoststr, int port,
                        uuid_t uuid)
{
        int                             ret = -1;
        glusterd_peerinfo_t             *peerinfo = NULL;
        glusterd_friend_sm_event_t      *event = NULL;
        glusterd_probe_ctx_t            *ctx = NULL;

        GF_ASSERT (hoststr);
        GF_ASSERT (req);

        ret = glusterd_friend_find (uuid, (char *)hoststr, &peerinfo);

        if (ret) {
                gf_log ("glusterd", GF_LOG_INFO, "Unable to find peerinfo"
                        " for host: %s %d", hoststr, port);
                goto out;
        }

        if (!peerinfo->rpc) {
                //handle this case
                goto out;
        }

        ret = glusterd_friend_sm_new_event
                (GD_FRIEND_EVENT_INIT_REMOVE_FRIEND, &event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to get new event");
                return ret;
        }

        ctx = GF_CALLOC (1, sizeof(*ctx), gf_gld_mt_probe_ctx_t);

        if (!ctx) {
                goto out;
        }

        ctx->hostname = gf_strdup (hoststr);
        ctx->port = port;
        ctx->req = req;

        event->ctx = ctx;

        event->peerinfo = peerinfo;

        ret = glusterd_friend_sm_inject_event (event);

        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to inject event %d, "
                        "ret = %d", event->event, ret);
                goto out;
        }

out:
        return ret;
}


int
glusterd_xfer_friend_remove_resp (rpcsvc_request_t *req, char *hostname, int port)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (hostname);

        rsp.op_ret = 0;
        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, conf->uuid);
        rsp.hostname = hostname;
        rsp.port = port;
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        return ret;
}


int
glusterd_xfer_friend_add_resp (rpcsvc_request_t *req, char *hostname, int port,
                               int32_t op_ret, int32_t op_errno)
{
        gd1_mgmt_friend_rsp  rsp = {{0}, };
        int32_t              ret = -1;
        xlator_t             *this = NULL;
        glusterd_conf_t      *conf = NULL;

        GF_ASSERT (hostname);

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        uuid_copy (rsp.uuid, conf->uuid);
        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = gf_strdup (hostname);
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_friend_rsp);

        gf_log ("glusterd", GF_LOG_INFO,
                "Responded to %s (%d), ret: %d", hostname, port, ret);
        if (rsp.hostname)
                GF_FREE (rsp.hostname)
        return ret;
}

int
glusterd_xfer_cli_probe_resp (rpcsvc_request_t *req, int32_t op_ret,
                              int32_t op_errno, char *hostname, int port)
{
        gf1_cli_probe_rsp    rsp = {0, };
        int32_t              ret = -1;

        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = hostname;
        rsp.port = port;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_probe_rsp);

        gf_log ("glusterd", GF_LOG_INFO, "Responded to CLI, ret: %d",ret);

        return ret;
}

int
glusterd_xfer_cli_deprobe_resp (rpcsvc_request_t *req, int32_t op_ret,
                                int32_t op_errno, char *hostname)
{
        gf1_cli_deprobe_rsp    rsp = {0, };
        int32_t                ret = -1;

        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = op_errno;
        rsp.hostname = hostname;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_deprobe_rsp);

        gf_log ("glusterd", GF_LOG_INFO, "Responded to CLI, ret: %d",ret);

        return ret;
}

int32_t
glusterd_list_friends (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_peerinfo_t     *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *friends = NULL;
        gf1_cli_peer_list_rsp   rsp = {0,};

        priv = THIS->private;
        GF_ASSERT (priv);

        if (!list_empty (&priv->peers)) {
                friends = dict_new ();
                if (!friends) {
                        gf_log ("", GF_LOG_WARNING, "Out of Memory");
                        goto out;
                }
        } else {
                ret = 0;
                goto out;
        }

        if (flags == GF_CLI_LIST_ALL) {
                        list_for_each_entry (entry, &priv->peers, uuid_list) {
                                count++;
                                ret = glusterd_add_peer_detail_to_dict (entry,
                                                                friends, count);
                                if (ret)
                                        goto out;

                        }

                        ret = dict_set_int32 (friends, "count", count);

                        if (ret)
                                goto out;
        }

        ret = dict_allocate_and_serialize (friends, &rsp.friends.friends_val,
                                           (size_t *)&rsp.friends.friends_len);

        if (ret)
                goto out;

        ret = 0;
out:

        if (friends)
                dict_unref (friends);

        rsp.op_ret = ret;

        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
        if (rsp.friends.friends_val)
                GF_FREE (rsp.friends.friends_val);

        return ret;
}

int32_t
glusterd_get_volumes (rpcsvc_request_t *req, dict_t *dict, int32_t flags)
{
        int32_t                 ret = -1;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *entry = NULL;
        int32_t                 count = 0;
        dict_t                  *volumes = NULL;
        gf_cli_rsp              rsp = {0,};
        char                    *volname = NULL;

        priv = THIS->private;
        GF_ASSERT (priv);

        volumes = dict_new ();
        if (!volumes) {
                gf_log ("", GF_LOG_WARNING, "Out of Memory");
                goto out;
        }

        if (list_empty (&priv->volumes)) {
                ret = 0;
                goto respond;
        }

        if (flags == GF_CLI_GET_VOLUME_ALL) {
                list_for_each_entry (entry, &priv->volumes, vol_list) {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                        volumes, count);
                        if (ret)
                                goto respond;

                        count++;

                }

        } else if (flags == GF_CLI_GET_NEXT_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);

                if (ret) {
                        if (priv->volumes.next) {
                                entry = list_entry (priv->volumes.next,
                                                    typeof (*entry),
                                                    vol_list);
                        }
                } else {
                        ret = glusterd_volinfo_find (volname, &entry);
                        if (ret)
                                goto respond;
                        entry = list_entry (entry->vol_list.next,
                                            typeof (*entry),
                                            vol_list);
                }

                if (&entry->vol_list == &priv->volumes) {
                       goto respond;
                } else {
                        ret = glusterd_add_volume_detail_to_dict (entry,
                                                         volumes, count);
                        if (ret)
                                goto respond;

                        count++;
                }
        } else if (flags == GF_CLI_GET_VOLUME) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret)
                        goto respond;

                ret = glusterd_volinfo_find (volname, &entry);
                if (ret)
                        goto respond;

                ret = glusterd_add_volume_detail_to_dict (entry,
                                                 volumes, count);
                if (ret)
                        goto respond;

                count++;
        }

respond:
        ret = dict_set_int32 (volumes, "count", count);
        if (ret)
                goto out;
        ret = dict_allocate_and_serialize (volumes, &rsp.dict.dict_val,
                                           (size_t *)&rsp.dict.dict_len);

        if (ret)
                goto out;

        ret = 0;
out:
        rsp.op_ret = ret;

        rsp.op_errstr = "";
        ret = glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gf_cli_rsp);

        if (volumes)
                dict_unref (volumes);

        if (rsp.dict.dict_val)
                GF_FREE (rsp.dict.dict_val);
        return ret;
}

int
glusterd_handle_status_volume (rpcsvc_request_t *req)
{
        int32_t                         ret     = -1;
        uint32_t                        cmd     = 0;
        dict_t                         *dict    = NULL;
        char                           *volname = 0;
        gf_cli_req                      cli_req = {{0,}};
        glusterd_op_t                   cli_op  = GD_OP_STATUS_VOLUME;

        GF_ASSERT (req);

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len > 0) {
                dict = dict_new();
                if (!dict)
                        goto out;
                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len, &dict);
                if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_ERROR, "failed to "
                                "unserialize buffer");
                        goto out;
                }

        }

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (!(cmd & GF_CLI_STATUS_ALL)) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "failed to get volname");
                        goto out;
                }
                gf_log (THIS->name, GF_LOG_INFO,
                        "Received status volume req "
                        "for volume %s", volname);

        }

        ret = glusterd_op_begin (req, GD_OP_STATUS_VOLUME, dict);

out:
        if (ret && dict)
                dict_unref (dict);

        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret)
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");
        if (cli_req.dict.dict_val)
                free (cli_req.dict.dict_val);

        return ret;
}

int
glusterd_brick_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event,
                          void *data)
{
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        int                     ret = 0;
        glusterd_brickinfo_t    *brickinfo = NULL;

        brickinfo = mydata;
        if (!brickinfo)
                return 0;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        switch (event) {
        case RPC_CLNT_CONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");
                glusterd_set_brick_status (brickinfo, GF_BRICK_STARTED);
                ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

                break;

        case RPC_CLNT_DISCONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT");
                glusterd_set_brick_status (brickinfo, GF_BRICK_STOPPED);
                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                break;
        }

        return ret;
}

int
glusterd_shd_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                         rpc_clnt_event_t event,
                         void *data)
{
        xlator_t                *this = NULL;
        glusterd_conf_t         *conf = NULL;
        int                     ret = 0;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        switch (event) {
        case RPC_CLNT_CONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");
                (void) glusterd_shd_set_running (_gf_true);
                ret = default_notify (this, GF_EVENT_CHILD_UP, NULL);

                break;

        case RPC_CLNT_DISCONNECT:
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT");
                (void) glusterd_shd_set_running (_gf_false);
                break;

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                break;
        }

        return ret;
}

int
glusterd_friend_remove_notify (glusterd_peerinfo_t *peerinfo, rpcsvc_request_t *req)
{
        int ret = -1;
        glusterd_friend_sm_event_t *new_event = NULL;

        ret = glusterd_friend_sm_new_event (GD_FRIEND_EVENT_REMOVE_FRIEND,
                                            &new_event);
        if (!ret) {
                new_event->peerinfo = peerinfo;
                ret = glusterd_friend_sm_inject_event (new_event);

                glusterd_friend_sm ();
                glusterd_op_sm ();

                if (!req) {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "Unable to find the request for responding "
                                "to User (%s)", peerinfo->hostname);
                        goto out;
                }

                glusterd_xfer_cli_probe_resp (req, -1, ENOTCONN,
                                              peerinfo->hostname, peerinfo->port);
        } else {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "Unable to create event for removing peer %s",
                        peerinfo->hostname);
        }

out:
        return ret;
}

int
glusterd_peer_rpc_notify (struct rpc_clnt *rpc, void *mydata,
                          rpc_clnt_event_t event,
                          void *data)
{
        xlator_t             *this        = NULL;
        glusterd_conf_t      *conf        = NULL;
        int                   ret         = 0;
        glusterd_peerinfo_t  *peerinfo    = NULL;
        glusterd_peerctx_t   *peerctx     = NULL;
        uuid_t                owner       = {0,};
        uuid_t               *peer_uuid   = NULL;

        peerctx = mydata;
        if (!peerctx)
                return 0;

        peerinfo = peerctx->peerinfo;
        this = THIS;
        conf = this->private;

        switch (event) {
        case RPC_CLNT_CONNECT:
        {
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_CONNECT");
                peerinfo->connected = 1;

                ret = glusterd_peer_handshake (this, rpc, peerctx);
                if (ret)
                        gf_log ("", GF_LOG_ERROR, "glusterd handshake failed");
                break;
        }

        case RPC_CLNT_DISCONNECT:
        {
                gf_log (this->name, GF_LOG_DEBUG, "got RPC_CLNT_DISCONNECT %d",
                        peerinfo->state.state);

                peerinfo->connected = 0;

                /*
                  local glusterd (thinks that it) is the owner of the cluster
                  lock and 'fails' the operation on the first disconnect from
                  a peer.
                */
                glusterd_get_lock_owner (&owner);
                if (!uuid_compare (conf->uuid, owner)) {
                        ret = glusterd_op_sm_inject_event (GD_OP_EVENT_START_UNLOCK,
                                                           NULL);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR, "Unable"
                                        " to enqueue cluster unlock event");
                        break;
                }

                peer_uuid = GF_CALLOC (1, sizeof (*peer_uuid), gf_common_mt_char);
                if (!peer_uuid) {
                        ret = -1;
                        break;
                }

                uuid_copy (*peer_uuid, peerinfo->uuid);
                ret = glusterd_op_sm_inject_event (GD_OP_EVENT_LOCAL_UNLOCK_NO_RESP,
                                                   peer_uuid);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "Unable"
                                " to enque local lock flush event.");

                //Inject friend disconnected here
                if (peerinfo->state.state == GD_FRIEND_STATE_DEFAULT)  {
                        /* Remove the friend as it was the newly requested
                           'peer' and connection with this peer didn't
                           succeed. we have opportunity to notify user
                        */
                        glusterd_friend_remove_notify (peerinfo,
                                                       peerctx->args.req);
                }

                //default_notify (this, GF_EVENT_CHILD_DOWN, NULL);
                break;
        }
        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        glusterd_friend_sm ();
        glusterd_op_sm ();
        return ret;
}

int
glusterd_null (rpcsvc_request_t *req)
{

        return 0;
}

rpcsvc_actor_t gd_svc_mgmt_actors[] = {
        [GLUSTERD_MGMT_NULL]           = { "NULL", GLUSTERD_MGMT_NULL, glusterd_null, NULL, NULL},
        [GLUSTERD_MGMT_CLUSTER_LOCK]   = { "CLUSTER_LOCK", GLUSTERD_MGMT_CLUSTER_LOCK, glusterd_handle_cluster_lock, NULL, NULL},
        [GLUSTERD_MGMT_CLUSTER_UNLOCK] = { "CLUSTER_UNLOCK", GLUSTERD_MGMT_CLUSTER_UNLOCK, glusterd_handle_cluster_unlock, NULL, NULL},
        [GLUSTERD_MGMT_STAGE_OP]       = { "STAGE_OP", GLUSTERD_MGMT_STAGE_OP, glusterd_handle_stage_op, NULL, NULL},
        [GLUSTERD_MGMT_COMMIT_OP]      = { "COMMIT_OP", GLUSTERD_MGMT_COMMIT_OP, glusterd_handle_commit_op, NULL, NULL},
};

struct rpcsvc_program gd_svc_mgmt_prog = {
        .progname  = "GlusterD svc mgmt",
        .prognum   = GD_MGMT_PROGRAM,
        .progver   = GD_MGMT_VERSION,
        .numactors = GLUSTERD_MGMT_MAXVALUE,
        .actors    = gd_svc_mgmt_actors,
};

rpcsvc_actor_t gd_svc_peer_actors[] = {
        [GLUSTERD_FRIEND_NULL]    = { "NULL", GLUSTERD_MGMT_NULL, glusterd_null, NULL, NULL},
        [GLUSTERD_PROBE_QUERY]    = { "PROBE_QUERY", GLUSTERD_PROBE_QUERY, glusterd_handle_probe_query, NULL, NULL},
        [GLUSTERD_FRIEND_ADD]     = { "FRIEND_ADD", GLUSTERD_FRIEND_ADD, glusterd_handle_incoming_friend_req, NULL, NULL},
        [GLUSTERD_FRIEND_REMOVE]  = { "FRIEND_REMOVE", GLUSTERD_FRIEND_REMOVE, glusterd_handle_incoming_unfriend_req, NULL, NULL},
        [GLUSTERD_FRIEND_UPDATE]  = { "FRIEND_UPDATE", GLUSTERD_FRIEND_UPDATE, glusterd_handle_friend_update, NULL, NULL},
};

struct rpcsvc_program gd_svc_peer_prog = {
        .progname  = "GlusterD svc peer",
        .prognum   = GD_FRIEND_PROGRAM,
        .progver   = GD_FRIEND_VERSION,
        .numactors = GLUSTERD_FRIEND_MAXVALUE,
        .actors    = gd_svc_peer_actors,
};



rpcsvc_actor_t gd_svc_cli_actors[] = {
        [GLUSTER_CLI_PROBE]         = { "CLI_PROBE", GLUSTER_CLI_PROBE, glusterd_handle_cli_probe, NULL, NULL},
        [GLUSTER_CLI_CREATE_VOLUME] = { "CLI_CREATE_VOLUME", GLUSTER_CLI_CREATE_VOLUME, glusterd_handle_create_volume, NULL,NULL},
        [GLUSTER_CLI_DEFRAG_VOLUME] = { "CLI_DEFRAG_VOLUME", GLUSTER_CLI_DEFRAG_VOLUME, glusterd_handle_defrag_volume, NULL,NULL},
        [GLUSTER_CLI_DEPROBE]       = { "FRIEND_REMOVE", GLUSTER_CLI_DEPROBE, glusterd_handle_cli_deprobe, NULL, NULL},
        [GLUSTER_CLI_LIST_FRIENDS]  = { "LIST_FRIENDS", GLUSTER_CLI_LIST_FRIENDS, glusterd_handle_cli_list_friends, NULL, NULL},
        [GLUSTER_CLI_START_VOLUME]  = { "START_VOLUME", GLUSTER_CLI_START_VOLUME, glusterd_handle_cli_start_volume, NULL, NULL},
        [GLUSTER_CLI_STOP_VOLUME]   = { "STOP_VOLUME", GLUSTER_CLI_STOP_VOLUME, glusterd_handle_cli_stop_volume, NULL, NULL},
        [GLUSTER_CLI_DELETE_VOLUME] = { "DELETE_VOLUME", GLUSTER_CLI_DELETE_VOLUME, glusterd_handle_cli_delete_volume, NULL, NULL},
        [GLUSTER_CLI_GET_VOLUME]    = { "GET_VOLUME", GLUSTER_CLI_GET_VOLUME, glusterd_handle_cli_get_volume, NULL, NULL},
        [GLUSTER_CLI_ADD_BRICK]     = { "ADD_BRICK", GLUSTER_CLI_ADD_BRICK, glusterd_handle_add_brick, NULL, NULL},
        [GLUSTER_CLI_REPLACE_BRICK] = { "REPLACE_BRICK", GLUSTER_CLI_REPLACE_BRICK, glusterd_handle_replace_brick, NULL, NULL},
        [GLUSTER_CLI_REMOVE_BRICK]  = { "REMOVE_BRICK", GLUSTER_CLI_REMOVE_BRICK, glusterd_handle_remove_brick, NULL, NULL},
        [GLUSTER_CLI_LOG_ROTATE]    = { "LOG FILENAME", GLUSTER_CLI_LOG_ROTATE, glusterd_handle_log_rotate, NULL, NULL},
        [GLUSTER_CLI_SET_VOLUME]    = { "SET_VOLUME", GLUSTER_CLI_SET_VOLUME, glusterd_handle_set_volume, NULL, NULL},
        [GLUSTER_CLI_SYNC_VOLUME]   = { "SYNC_VOLUME", GLUSTER_CLI_SYNC_VOLUME, glusterd_handle_sync_volume, NULL, NULL},
        [GLUSTER_CLI_RESET_VOLUME]  = { "RESET_VOLUME", GLUSTER_CLI_RESET_VOLUME, glusterd_handle_reset_volume, NULL, NULL},
        [GLUSTER_CLI_FSM_LOG]       = { "FSM_LOG", GLUSTER_CLI_FSM_LOG, glusterd_handle_fsm_log, NULL, NULL},
        [GLUSTER_CLI_GSYNC_SET]     = { "GSYNC_SET", GLUSTER_CLI_GSYNC_SET, glusterd_handle_gsync_set, NULL, NULL},
        [GLUSTER_CLI_PROFILE_VOLUME] = { "STATS_VOLUME", GLUSTER_CLI_PROFILE_VOLUME, glusterd_handle_cli_profile_volume, NULL, NULL},
        [GLUSTER_CLI_QUOTA]         = { "QUOTA", GLUSTER_CLI_QUOTA, glusterd_handle_quota, NULL, NULL},
        [GLUSTER_CLI_GETWD]         = { "GETWD", GLUSTER_CLI_GETWD, glusterd_handle_getwd, NULL, NULL},
        [GLUSTER_CLI_STATUS_VOLUME]  = {"STATUS_VOLUME", GLUSTER_CLI_STATUS_VOLUME, glusterd_handle_status_volume, NULL, NULL},
        [GLUSTER_CLI_MOUNT]         = { "MOUNT", GLUSTER_CLI_MOUNT, glusterd_handle_mount, NULL, NULL},
        [GLUSTER_CLI_UMOUNT]        = { "UMOUNT", GLUSTER_CLI_UMOUNT, glusterd_handle_umount, NULL, NULL},
        [GLUSTER_CLI_HEAL_VOLUME]  = { "HEAL_VOLUME", GLUSTER_CLI_HEAL_VOLUME, glusterd_handle_cli_heal_volume, NULL, NULL},
        [GLUSTER_CLI_STATEDUMP_VOLUME] = {"STATEDUMP_VOLUME", GLUSTER_CLI_STATEDUMP_VOLUME, glusterd_handle_cli_statedump_volume, NULL, NULL},
};

struct rpcsvc_program gd_svc_cli_prog = {
        .progname  = "GlusterD svc cli",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numactors = GLUSTER_CLI_MAXVALUE,
        .actors    = gd_svc_cli_actors,
};
