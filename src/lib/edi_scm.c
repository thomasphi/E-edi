#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_File.h>
#include <Ethumb.h>

#include "Edi.h"
#include "edi_private.h"
#include "edi_exe.h"
#include "edi_path.h"
#include "edi_scm.h"
#include "md5.h"

Edi_Scm_Engine *_edi_scm_global_object = NULL;

static int
_edi_scm_exec(const char *command)
{
   int code;
   char *oldpwd;
   Edi_Scm_Engine *self = _edi_scm_global_object;

   if (!self) return -1;

   oldpwd = getcwd(NULL, PATH_MAX);

   chdir(edi_project_get());
   code = edi_exe_wait(command);
   chdir(oldpwd);

   free(oldpwd);

   return code;
}

static char *
_edi_scm_exec_response(const char *command)
{
   char *oldpwd, *response;
   Edi_Scm_Engine *self = _edi_scm_global_object;

   if (!self) return NULL;

   oldpwd = getcwd(NULL, PATH_MAX);

   chdir(edi_project_get());
   response = edi_exe_response(command);
   chdir(oldpwd);

   free(oldpwd);

   return response;
}

EAPI int
edi_scm_git_new(void)
{
   return _edi_scm_exec("git init .");
}

EAPI int
edi_scm_git_clone(const char *url, const char *dir)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git clone '%s' '%s'", url, dir);
   code = edi_exe_wait(eina_strbuf_string_get(command));

   eina_strbuf_free(command);
   return code;
}

static int
_edi_scm_git_file_add(const char *path)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git add '%s'", path);

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_file_mod(const char *path)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git mod '%s'", path);

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_file_move(const char *source, const char *dest)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git mv %s %s", source, dest);

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_file_del(const char *path)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git rm '%s'", path);

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_status(void)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append(command, "git status");

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static Edi_Scm_Status *
_parse_line(char *line)
{
   char *path, *change;
   Edi_Scm_Status *status;

   change = line;
   line[2] = '\0';
   path = line + 3;

   status = malloc(sizeof(Edi_Scm_Status));
   if (!status)
     return NULL;

   status->staged = EINA_FALSE;

   if (change[0] == 'A' || change[1] == 'A')
     {
        status->change = EDI_SCM_STATUS_ADDED;
        if (change[0] == 'A')
          {
            status->staged = status->change = EDI_SCM_STATUS_ADDED_STAGED;
          }
     }
   else if (change[0] == 'R' || change[1] == 'R')
     {
        status->change = EDI_SCM_STATUS_RENAMED;
        if (change[0] == 'R')
          {
             status->staged = status->change = EDI_SCM_STATUS_RENAMED_STAGED;
          }
     }
   else if (change[0] == 'M' || change[1] == 'M')
     {
        status->change = EDI_SCM_STATUS_MODIFIED;
        if (change[0] == 'M')
          {
             status->staged = status->change = EDI_SCM_STATUS_MODIFIED_STAGED;
          }
     }
   else if (change[0] == 'D' || change[1] == 'D')
     {
        status->change = EDI_SCM_STATUS_DELETED;
        if (change[0] == 'D')
          {
             status->staged = status->change = EDI_SCM_STATUS_DELETED_STAGED;
          }
     }
   else if (change[0] == '?' && change[1] == '?')
     {
        status->change = EDI_SCM_STATUS_UNTRACKED;
     }
   else
        status->change = EDI_SCM_STATUS_UNKNOWN;

   status->path = eina_stringshare_add(path);

   return status;
}

static Edi_Scm_Status_Code
_edi_scm_git_file_status(const char *path)
{
   Edi_Scm_Status *status;
   char command[4096];
   char *line;
   Edi_Scm_Status_Code result;

   snprintf(command, sizeof(command), "git status --porcelain '%s'", path);

   line = _edi_scm_exec_response(command);
   if (!line[0] || !line[1])
     {
        result = EDI_SCM_STATUS_NONE;
     }
   else
     {
        status = _parse_line(line);
        result = status->change;
        eina_stringshare_del(status->path);
        free(status);
     }

   free(line);

   return result;
}

static Eina_List *
_edi_scm_git_status_get(void)
{
   char *output, *pos, *start, *end;
   char *line;
   size_t size;
   Eina_Strbuf *command;
   Edi_Scm_Status *status;
   Eina_List *list = NULL;

   command = eina_strbuf_new();

   eina_strbuf_append(command, "git status --porcelain");

   output = _edi_scm_exec_response(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   end = NULL;

   pos = output;
   start = pos;

   while (*pos++)
     {
        if (*pos == '\n')
          end = pos;
        if (start && end)
          {
             size = end - start;
             line = malloc(size + 1);
             memcpy(line, start, size);
             line[size] = '\0';

             status = _parse_line(line);
             if (status)
               list = eina_list_append(list, status);

             free(line);
             start = end + 1;
             end = NULL;
          }
     }

   end = pos;
   size = end - start;
   if (size > 1)
     {
        line = malloc(size + 1);
        memcpy(line, start, size);
        line[size] = '\0';

        status = _parse_line(line);
        if (status)
          list = eina_list_append(list, status);

        free(line);
    }

   free(output);

   return list;
}

static char *
_edi_scm_git_diff(void)
{
   char *output;
   Eina_Strbuf *command;

   command = eina_strbuf_new();

   eina_strbuf_append(command, "git diff");

   output = _edi_scm_exec_response(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return output;
}

static int
_edi_scm_git_commit(const char *message)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git commit -m '%s'", message);

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_push(void)
{
   return _edi_scm_exec("git push");
}

static int
_edi_scm_git_pull(void)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append(command, "git pull");

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_stash(void)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append(command, "git stash");

   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static int
_edi_scm_git_remote_add(const char *remote_url)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git remote add origin %s", remote_url);

   code = _edi_scm_exec(eina_strbuf_string_get(command));
   eina_strbuf_free(command);

   if (code == 0)
     code = _edi_scm_exec("git push --set-upstream origin master");

   return code;
}

static const char *
_edi_scm_git_remote_name_get(void)
{
   static char *_remote_name;
   Edi_Scm_Engine *engine = _edi_scm_global_object;

   if (!engine)
     return NULL;

   if (!_remote_name)
     _remote_name = _edi_scm_exec_response("git config --get user.name");

   return _remote_name;
}

static const char *
_edi_scm_git_remote_email_get(void)
{
   static char *_remote_email;
   Edi_Scm_Engine *engine = _edi_scm_global_object;

   if (!engine)
     return NULL;

   if (!_remote_email)
     _remote_email = _edi_scm_exec_response("git config --get user.email");

   return _remote_email;
}

static const char *
_edi_scm_git_remote_url_get(void)
{
   static char *_remote_url;
   Edi_Scm_Engine *engine = _edi_scm_global_object;

   if (!engine)
     return NULL;

   if (!_remote_url)
     _remote_url = _edi_scm_exec_response("git remote get-url origin");

   return _remote_url;
}

static int
_edi_scm_git_credentials_set(const char *name, const char *email)
{
   int code;
   Eina_Strbuf *command = eina_strbuf_new();

   eina_strbuf_append_printf(command, "git config user.name '%s'", name);
   code = _edi_scm_exec(eina_strbuf_string_get(command));
   eina_strbuf_reset(command);
   eina_strbuf_append_printf(command, "git config user.email '%s'", email);
   code = _edi_scm_exec(eina_strbuf_string_get(command));

   eina_strbuf_free(command);

   return code;
}

static Eina_Bool
_edi_scm_enabled(Edi_Scm_Engine *engine)
{
   char *path;
   if (!engine) return EINA_FALSE;

   if (!engine->path)
     {
        path = edi_path_append(edi_project_get(), engine->directory);
        engine->path = eina_stringshare_add(path);
        free(path);
     }

   return ecore_file_exists(engine->path);
}

EAPI Eina_Bool
edi_scm_remote_enabled(void)
{
   Edi_Scm_Engine *e = _edi_scm_global_object;
   if (!e)
     return EINA_FALSE;

   return !!e->remote_url_get();
}

EAPI Eina_Bool
edi_scm_enabled(void)
{
   Edi_Scm_Engine *engine = _edi_scm_global_object;
   if (!engine)
     return EINA_FALSE;

   if (!engine->initialized)
     return EINA_FALSE;

   return _edi_scm_enabled(engine);
}

EAPI Edi_Scm_Engine *
edi_scm_engine_get(void)
{
   Edi_Scm_Engine *engine = _edi_scm_global_object;
   if (!engine)
     return NULL;

   return engine;
}

EAPI void
edi_scm_shutdown()
{
   Edi_Scm_Engine *engine = _edi_scm_global_object;

   if (!engine)
     return;

   eina_stringshare_del(engine->name);
   eina_stringshare_del(engine->directory);
   eina_stringshare_del(engine->path);
   free(engine);

   _edi_scm_global_object = NULL;
}

EAPI int
edi_scm_add(const char *path)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->file_add(path);
}

EAPI int
edi_scm_del(const char *path)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->file_del(path);
}

EAPI int
edi_scm_move(const char *src, const char *dest)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->move(src, dest);
}

EAPI Eina_Bool
edi_scm_status_get(void)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   e->statuses = e->status_get();

   if (!e->statuses)
     return EINA_FALSE;

   return EINA_TRUE;
}

EAPI Edi_Scm_Status_Code
edi_scm_file_status(const char *path)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->file_status(path);
}

static void
_edi_scm_commit_thread_cb(void *data, Ecore_Thread *thread)
{
   Edi_Scm_Engine *e;
   const char *message = data;

   e = edi_scm_engine_get();

   e->commit(message);

   ecore_thread_cancel(thread);
}

EAPI void
edi_scm_commit(const char *message)
{
   ecore_thread_run(_edi_scm_commit_thread_cb, NULL, NULL, message);
}

static void
_edi_scm_status_thread_cb(void *data, Ecore_Thread *thread)
{
   Edi_Scm_Engine *e = data;

   e->status();

   ecore_thread_cancel(thread);
}

EAPI void
edi_scm_status(void)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   ecore_thread_run(_edi_scm_status_thread_cb, NULL, NULL, e);
}

EAPI int
edi_scm_remote_add(const char *remote_url)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->remote_add(remote_url);
}

EAPI char *
edi_scm_diff(void)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->diff();
}

static void
_edi_scm_stash_thread_cb(void *data, Ecore_Thread *thread)
{
   Edi_Scm_Engine *e = data;

   e->stash();

   ecore_thread_cancel(thread);
}

EAPI void
edi_scm_stash(void)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   ecore_thread_run(_edi_scm_stash_thread_cb, NULL, NULL, e);
}

EAPI int
edi_scm_credentials_set(const char *user, const char *email)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   return e->credentials_set(user, email);
}

static void
_edi_scm_pull_thread_cb(void *data, Ecore_Thread *thread)
{
   Edi_Scm_Engine *e = data;

   e->pull();

   ecore_thread_cancel(thread);
}

EAPI void
edi_scm_pull(void)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   ecore_thread_run(_edi_scm_pull_thread_cb, NULL, NULL, e);
}

static void
_edi_scm_push_thread_cb(void *data, Ecore_Thread *thread)
{
   Edi_Scm_Engine *e = data;

   e->push();

   ecore_thread_cancel(thread);
}

EAPI void
edi_scm_push(void)
{
   Edi_Scm_Engine *e = edi_scm_engine_get();

   ecore_thread_run(_edi_scm_push_thread_cb, NULL, NULL, e);
}

static Edi_Scm_Engine *
_edi_scm_git_init()
{
   Edi_Scm_Engine *engine;

   if (!ecore_file_app_installed("git"))
     return NULL;

   _edi_scm_global_object = engine = calloc(1, sizeof(Edi_Scm_Engine));
   engine->name = eina_stringshare_add("git");
   engine->directory = eina_stringshare_add(".git");
   engine->file_add = _edi_scm_git_file_add;
   engine->file_mod = _edi_scm_git_file_mod;
   engine->file_del = _edi_scm_git_file_del;
   engine->move = _edi_scm_git_file_move;
   engine->status = _edi_scm_git_status;
   engine->diff = _edi_scm_git_diff;
   engine->commit = _edi_scm_git_commit;
   engine->pull = _edi_scm_git_pull;
   engine->push = _edi_scm_git_push;
   engine->stash = _edi_scm_git_stash;
   engine->file_status = _edi_scm_git_file_status;

   engine->remote_add = _edi_scm_git_remote_add;
   engine->remote_name_get = _edi_scm_git_remote_name_get;
   engine->remote_email_get = _edi_scm_git_remote_email_get;
   engine->remote_url_get = _edi_scm_git_remote_url_get;
   engine->credentials_set = _edi_scm_git_credentials_set;
   engine->status_get = _edi_scm_git_status_get;

   engine->initialized = EINA_TRUE;

   return engine;
}

EAPI Edi_Scm_Engine *
edi_scm_init(void)
{
   if (edi_project_file_exists(".git"))
     return _edi_scm_git_init();

   return NULL;
}

EAPI const char *
edi_scm_avatar_url_get(const char *email)
{
   int n;
   char *id;
   const char *url;
   MD5_CTX ctx;
   char md5out[(2 * MD5_HASHBYTES) + 1];
   unsigned char hash[MD5_HASHBYTES];
   static const char hex[] = "0123456789abcdef";

   if (!email || strlen(email) == 0)
     return NULL;

   id = strdup(email);
   eina_str_tolower(&id);

   MD5Init(&ctx);
   MD5Update(&ctx, (unsigned char const*)id, (unsigned)strlen(id));
   MD5Final(hash, &ctx);

   for (n = 0; n < MD5_HASHBYTES; n++)
     {
        md5out[2 * n] = hex[hash[n] >> 4];
        md5out[2 * n + 1] = hex[hash[n] & 0x0f];
     }
   md5out[2 * MD5_HASHBYTES] = '\0';

   url = eina_stringshare_printf("http://www.gravatar.com/avatar/%s", md5out);

   free(id);
   return url;
}

