#include "erl_nif.h"
#include <string.h>
#include <stdio.h>

#include "queue.h"
#include "duktape/duktape.h"

static ErlNifResourceType *duxtape_connection_type = NULL;

/* duxtape context connection context */
typedef struct {
    ErlNifTid tid;
    ErlNifThreadOpts* opts;
    ErlNifPid notification_pid;

    duk_context *ctx;
    queue *commands;

} duxtape_connection;

typedef enum {
    cmd_unknown,
    cmd_open,
    cmd_close,
    cmd_stop,
    cmd_eval
} command_type;

typedef struct {
    command_type type;

    ErlNifEnv *env;
    ERL_NIF_TERM ref;
    ErlNifPid pid;
    ERL_NIF_TERM arg;
} duxtape_command;

static ERL_NIF_TERM atom_duxtape;

static ERL_NIF_TERM push_command(ErlNifEnv *env, duxtape_connection *conn, duxtape_command *cmd);

static ERL_NIF_TERM
make_atom(ErlNifEnv *env, const char *atom_name)
{
    ERL_NIF_TERM atom;

    if(enif_make_existing_atom(env, atom_name, &atom, ERL_NIF_LATIN1))
	   return atom;

    return enif_make_atom(env, atom_name);
}

static ERL_NIF_TERM
make_ok_tuple(ErlNifEnv *env, ERL_NIF_TERM value)
{
    return enif_make_tuple2(env, make_atom(env, "ok"), value);
}

static ERL_NIF_TERM
make_error_tuple(ErlNifEnv *env, const char *reason)
{
    return enif_make_tuple2(env, make_atom(env, "error"), make_atom(env, reason));
}

static void
command_destroy(void *obj)
{
    duxtape_command *cmd = (duxtape_command *) obj;

    if(cmd->env != NULL)
	   enif_free_env(cmd->env);

    enif_free(cmd);
}

static duxtape_command *
command_create()
{
    duxtape_command *cmd = (duxtape_command *) enif_alloc(sizeof(duxtape_command));
    if(cmd == NULL)
	   return NULL;

    cmd->env = enif_alloc_env();
    if(cmd->env == NULL) {
	    command_destroy(cmd);
        return NULL;
    }

    cmd->type = cmd_unknown;
    cmd->ref = 0;
    cmd->arg = 0;

    return cmd;
}

/*
 *
 */
static void
destruct_duxtape_connection(ErlNifEnv *env, void *arg)
{
    duxtape_connection *conn = (duxtape_connection *) arg;
    duxtape_command *cmd = command_create();

    /* Send the stop command
     */
    cmd->type = cmd_stop;
    queue_push(conn->commands, cmd);

    /* Wait for the thread to finish
     */
    enif_thread_join(conn->tid, NULL);

    enif_thread_opts_destroy(conn->opts);

    /* The thread has finished... now remove the command queue, and close
     * the duxtape context (if it was still open).
     */
    while(queue_has_item(conn->commands)) {
        command_destroy(queue_pop(conn->commands));
    }
    queue_destroy(conn->commands);

    duk_destroy_heap(conn->ctx);
    conn->ctx = NULL;
}

static duk_ret_t native_print(duk_context *ctx) {
	duk_push_string(ctx, " ");
	duk_insert(ctx, 0);
	duk_join(ctx, duk_get_top(ctx) - 1);
	enif_fprintf(stderr, "%s\n", duk_to_string(ctx, -1));
	return 0;
}

static ERL_NIF_TERM
do_open(ErlNifEnv *env, duxtape_connection *conn, const ERL_NIF_TERM arg)
{
    ERL_NIF_TERM error;

    /** TODO(Connor) - change this to: 
        duk_context *ctx = duk_create_heap(my_alloc,
                                           my_realloc,
                                           my_free,
                                           my_udata,
                                           my_fatal);
    */
    /* Open the duxtape context. */
    conn->ctx = duk_create_heap_default();
    duk_push_c_function(conn->ctx, native_print, DUK_VARARGS);
	  duk_put_global_string(conn->ctx, "print");
    if (conn->ctx) {
      return make_atom(env, "ok");
    } else {
      error = make_error_tuple(env, "context_open_fail");
      conn->ctx = NULL;
	    return error;
    }
}

static ERL_NIF_TERM
do_close(ErlNifEnv *env, duxtape_connection *conn, const ERL_NIF_TERM arg)
{
    duk_destroy_heap(conn->ctx);
    conn->ctx = NULL;
    return make_atom(env, "ok");
}


static ERL_NIF_TERM
encode_dux_return(ErlNifEnv *env, duxtape_connection *conn, const char* status)
{
    duk_context* ctx = conn->ctx;
    ERL_NIF_TERM ret;
    switch(duk_get_type(ctx, -1)) {
        case DUK_TYPE_NONE: 
            ret = make_atom(env, "none");
            break;
        case DUK_TYPE_UNDEFINED: 
            ret = make_atom(env, "undefined");
            break;
        case DUK_TYPE_NULL: 
            ret = make_atom(env, "nil");
            break;
        case DUK_TYPE_BOOLEAN:
            ret =  duk_get_boolean(ctx, -1) ? make_atom(env, "true") : make_atom(env, "false");
            break;
        case DUK_TYPE_NUMBER: 
            ret = enif_make_double(env, duk_get_number(ctx, -1));
            break;
        case DUK_TYPE_STRING:
            ret = enif_make_string(env, duk_safe_to_string(conn->ctx, -1), ERL_NIF_LATIN1);
            break;
        case DUK_TYPE_OBJECT: 
            ret = enif_make_atom(env, "DUK_TYPE_OBJECT");
            break;
        case DUK_TYPE_BUFFER: 
            ret = enif_make_atom(env, "DUK_TYPE_BUFFER");
            break;
        case DUK_TYPE_POINTER: 
            ret = enif_make_atom(env, "DUK_TYPE_POINTER");
            break;            
        case DUK_TYPE_LIGHTFUNC: 
            ret = enif_make_atom(env, "DUK_TYPE_LIGHTFUNC");
            break; 
        default:
            ret = enif_make_atom(env, "unknown_type");
            break;
    }
    duk_pop(ctx);
    return enif_make_tuple2(env, make_atom(env, status), ret);
}

static ERL_NIF_TERM
do_eval(ErlNifEnv *env, duxtape_connection *conn, const ERL_NIF_TERM arg)
{
  unsigned int len = 0;
  if(!enif_is_list(env, arg)) return make_error_tuple(env, "not_list");

  int rc = enif_get_list_length(env, arg, &len);

  if(!rc) return make_error_tuple(env, "list_length");

  char *buf = enif_alloc(500);
  if(!buf) return make_error_tuple(env, "alloc");

  rc = enif_get_string(env, arg, buf, len, ERL_NIF_LATIN1);
  if(rc != len) {
    enif_fprintf(stderr, "get_string=%d length=%d\r\n", rc, len);
    return make_error_tuple(env, "get_string");
  } 

  const char *str = duk_push_string(conn->ctx, buf);
  enif_fprintf(stderr, "evaling: %s\r\n", str);
  duk_peval(conn->ctx);
  if (duk_peval(conn->ctx) != 0) {
    enif_fprintf(stderr, "eval failed: %s\n", duk_safe_to_string(conn->ctx, -1));
    return encode_dux_return(env, conn, "error");
  } else {
    // enif_fprintf(stderr, "result is: %s\n", duk_safe_to_string(conn->ctx, -1));
    return encode_dux_return(env, conn, "ok");
  }
//   duk_pop(conn->ctx);
//   return make_atom(env, "ok");
}

static ERL_NIF_TERM
evaluate_command(duxtape_command *cmd, duxtape_connection *conn)
{
    switch(cmd->type) {
      case cmd_open:
        return do_open(cmd->env, conn, cmd->arg);
      case cmd_close:
        return do_close(cmd->env, conn, cmd->arg);
      case cmd_eval:
        return do_eval(cmd->env, conn, cmd->arg);
      default:
        return make_error_tuple(cmd->env, "invalid_command");
    }
}

static ERL_NIF_TERM
push_command(ErlNifEnv *env, duxtape_connection *conn, duxtape_command *cmd) {
    if(!queue_push(conn->commands, cmd))
        return make_error_tuple(env, "command_push_failed");

    return make_atom(env, "ok");
}

static ERL_NIF_TERM
make_answer(duxtape_command *cmd, ERL_NIF_TERM answer)
{
    return enif_make_tuple3(cmd->env, atom_duxtape, cmd->ref, answer);
}

static void *
duxtape_connection_run(void *arg)
{
    duxtape_connection *conn = (duxtape_connection *) arg;
    duxtape_command *cmd;
    int continue_running = 1;

    while(continue_running) {
	    cmd = queue_pop(conn->commands);

	    if(cmd->type == cmd_stop) {
	        continue_running = 0;
        } else {
	        enif_send(NULL, &cmd->pid, cmd->env, make_answer(cmd, evaluate_command(cmd, conn)));
        }

	    command_destroy(cmd);
    }

    return NULL;
}

/*
 * Start the processing thread
 */
static ERL_NIF_TERM
duxtape_start(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    duxtape_connection *conn;
    ERL_NIF_TERM duxtape_conn;

    /* Initialize the resource */
    conn = enif_alloc_resource(duxtape_connection_type, sizeof(duxtape_connection));
    if(!conn)
	    return make_error_tuple(env, "no_memory");

    conn->ctx = NULL;

    /* Create command queue */
    conn->commands = queue_create();
    if(!conn->commands) {
	    enif_release_resource(conn);
	    return make_error_tuple(env, "command_queue_create_failed");
    }

    /* Start command processing thread */
    conn->opts = enif_thread_opts_create("duxtape_thread_opts");
    if(enif_thread_create("duxtape_connection", &conn->tid, duxtape_connection_run, conn, conn->opts) != 0) {
	    enif_release_resource(conn);
	    return make_error_tuple(env, "thread_create_failed");
    }

    duxtape_conn = enif_make_resource(env, conn);
    enif_release_resource(conn);

    return make_ok_tuple(env, duxtape_conn);
}

/*
 * Open the duxtape context
 */
static ERL_NIF_TERM
duxtape_open(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    duxtape_connection *conn;
    duxtape_command *cmd = NULL;
    ErlNifPid pid;

    if(argc != 4)
	    return enif_make_badarg(env);
    if(!enif_get_resource(env, argv[0], duxtape_connection_type, (void **) &conn))
	    return enif_make_badarg(env);
    if(!enif_is_ref(env, argv[1]))
	    return make_error_tuple(env, "invalid_ref");
    if(!enif_get_local_pid(env, argv[2], &pid))
	    return make_error_tuple(env, "invalid_pid");

    /* Note, no check is made for the type of the argument */
    cmd = command_create();
    if(!cmd)
	    return make_error_tuple(env, "command_create_failed");

    cmd->type = cmd_open;
    cmd->ref = enif_make_copy(cmd->env, argv[1]);
    cmd->pid = pid;
    cmd->arg = enif_make_copy(cmd->env, argv[3]);

    return push_command(env, conn, cmd);
}

/*
 * Close the duxtape context
 */
static ERL_NIF_TERM
duxtape_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    duxtape_connection *conn;
    duxtape_command *cmd = NULL;
    ErlNifPid pid;

    if(!enif_get_resource(env, argv[0], duxtape_connection_type, (void **) &conn))
	    return enif_make_badarg(env);
    if(!enif_is_ref(env, argv[1]))
	    return make_error_tuple(env, "invalid_ref");
    if(!enif_get_local_pid(env, argv[2], &pid))
	    return make_error_tuple(env, "invalid_pid");

    cmd = command_create();
    if(!cmd)
	    return make_error_tuple(env, "command_create_failed");

    cmd->type = cmd_close;
    cmd->ref = enif_make_copy(cmd->env, argv[1]);
    cmd->pid = pid;

    return push_command(env, conn, cmd);
}

static ERL_NIF_TERM
duxtape_eval(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    duxtape_connection *conn;
    duxtape_command *cmd = NULL;
    ErlNifPid pid;

    if(!enif_get_resource(env, argv[0], duxtape_connection_type, (void **) &conn))
	    return enif_make_badarg(env);
    if(!enif_is_ref(env, argv[1]))
	    return make_error_tuple(env, "invalid_ref");
    if(!enif_get_local_pid(env, argv[2], &pid))
	    return make_error_tuple(env, "invalid_pid");
    if(!enif_is_list(env, argv[3]))
      return make_error_tuple(env, "invalid_str");

    cmd = command_create();
    if(!cmd)
	    return make_error_tuple(env, "command_create_failed");

    cmd->type = cmd_eval;
    cmd->ref = enif_make_copy(cmd->env, argv[1]);
    cmd->arg = enif_make_copy(cmd->env, argv[3]);
    cmd->pid = pid;

    return push_command(env, conn, cmd);
}

/*
 * Load the nif. Initialize some stuff and such
 */
static int
on_load(ErlNifEnv* env, void** priv, ERL_NIF_TERM info)
{
    ErlNifResourceType *rt;

    rt = enif_open_resource_type(env, "duxtape_nif", "duxtape_connection_type",
				destruct_duxtape_connection, ERL_NIF_RT_CREATE, NULL);
    if(!rt)
	    return -1;
    duxtape_connection_type = rt;

    atom_duxtape = make_atom(env, "duxtape");

    return 0;
}

static int on_reload(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    return 0;
}

static int on_upgrade(ErlNifEnv* env, void** priv, void** old_priv_data, ERL_NIF_TERM load_info)
{
    return 0;
}

static ErlNifFunc nif_funcs[] = {
    {"start", 0, duxtape_start},
    {"open", 4, duxtape_open},
    {"close", 3, duxtape_close},
    {"eval", 4, duxtape_eval},
};

ERL_NIF_INIT(Elixir.DuxTapeNif, nif_funcs, on_load, on_reload, on_upgrade, NULL);