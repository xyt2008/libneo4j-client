/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2016, Chris Leishman (http://github.com/cleishm)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "../../config.h"
#include "tofu.h"
#include "client_config.h"
#include "logging.h"
#include "util.h"
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <sys/file.h>
#include <unistd.h>

#define NEO4J_KNOWN_HOSTS "neo4j_known_certs"
#define NEO4J_MAX_HOST_LENGTH 512
#define NEO4J_MAX_FINGERPRINT_LENGTH 60
#define NEO4J_MAX_KNOWN_HOSTS_LINE_LENGTH 1024
#define NEO4J_TEMP_FILE_SUFFIX ".tmpXXXXXX"

static int retrieve_stored_fingerprint(const char *file, const char *host,
        char *buf, size_t n, neo4j_logger_t *logger);
static int update_stored_fingerprint(const char *file, const char *host,
        char *fingerprint, neo4j_logger_t *logger);


int neo4j_check_known_hosts(const char *hostname, int port,
        char *fingerprint, const neo4j_config_t *config, uint_fast8_t flags)
{
    neo4j_logger_t *logger = neo4j_get_logger(config, "tofu");

    REQUIRE(strlen(hostname) < 256 && hostname[0] != '\0', -1);

    char buf[PATH_MAX];
    const char *file = config->known_hosts_file;
    if (file == NULL)
    {
        if (neo4j_dot_dir(buf, sizeof(buf), NEO4J_KNOWN_HOSTS) < 0)
        {
            if (errno == ERANGE)
            {
                errno = ENAMETOOLONG;
            }
            return -1;
        }
        file = buf;
    }

    char host[NEO4J_MAX_HOST_LENGTH];
    int n = snprintf(host, sizeof(host), "%s:%d", hostname, port);
    assert(n > 0 && n < NEO4J_MAX_HOST_LENGTH);

    char existing[NEO4J_MAX_FINGERPRINT_LENGTH];
    int result = retrieve_stored_fingerprint(file, host,
                existing, sizeof(existing), logger);

    if (result > 0 || strcmp(fingerprint, existing) != 0)
    {
        neo4j_unverified_host_reason_t reason = (result == 0)?
            NEO4J_HOST_VERIFICATION_MISMATCH :
            NEO4J_HOST_VERIFICATION_UNRECOGNIZED;
        result = 1;
        if (config->unverified_host_callback != NULL)
        {
            int action = config->unverified_host_callback(
                    config->unverified_host_callback_userdata, host,
                    fingerprint, reason);
            switch (action)
            {
            case NEO4J_HOST_VERIFICATION_TRUST:
                if (update_stored_fingerprint(file, host, fingerprint, logger))
                {
                    return -1;
                }
                // fall through
            case NEO4J_HOST_VERIFICATION_ACCEPT_ONCE:
                result = 0;
                break;
            default:
                break;
            }
        }
    }

    return result;
}


int retrieve_stored_fingerprint(const char *file, const char *host,
        char *buf, size_t n, neo4j_logger_t *logger)
{
    char ebuf[256];

    FILE *stream = fopen(file, "r");
    if (stream == NULL)
    {
        if (errno == ENOENT)
        {
            return 1;
        }
        neo4j_log_error(logger, "Failed to open '%s': %s", file,
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        return -1;
    }

    int result = 1;

    char line[NEO4J_MAX_KNOWN_HOSTS_LINE_LENGTH];
    size_t hostlen = strlen(host);
    while (fgets(line, sizeof(line), stream) != NULL)
    {
        if (strncmp(line, host, hostlen) == 0 && isspace(line[hostlen]))
        {
            char *p = line + hostlen + 1;
            for (; isspace(*p); ++p)
                ;
            size_t l = strlen(p);
            for (; l > 0 && isspace(p[l-1]); --l)
                ;
            l = min(l, n - 1);
            memcpy(buf, p, l);
            buf[l] = '\0';
            result = 0;
            goto cleanup;
        }
    }

    if (!feof(stream))
    {
        errno = ferror(stream);
        neo4j_log_error(logger, "Failed reading '%s': %s", file,
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        result = -1;
    }

    int errsv;
cleanup:
    errsv = errno;
    fclose(stream);
    errno = errsv;
    return result;
}


int update_stored_fingerprint(const char *file, const char *host,
        char *fingerprint, neo4j_logger_t *logger)
{
    char ebuf[256];

    FILE *in_stream = fopen(file, "r");
    if (in_stream == NULL && errno != ENOENT)
    {
        neo4j_log_error(logger, "Failed to open '%s': %s", file,
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        return -1;
    }

    int out_fd = -1;
    FILE *out_stream = NULL;

    size_t filelen = strlen(file);
    size_t suffixlen = strlen(NEO4J_TEMP_FILE_SUFFIX);
    char outfile[PATH_MAX];
    if ((filelen + suffixlen + 1) > PATH_MAX)
    {
        neo4j_log_error(logger, "Temporary filename too long");
        errno = ENAMETOOLONG;
        goto failure;
    }

    neo4j_dirname(file, outfile, sizeof(outfile));
    if (neo4j_mkdir_p(outfile))
    {
        neo4j_log_error(logger, "Failed to create directory '%s': %s", outfile,
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        goto failure;
    }

    memcpy(outfile, file, filelen);
    memcpy(outfile + filelen, NEO4J_TEMP_FILE_SUFFIX, suffixlen);
    outfile[filelen + suffixlen] = '\0';

    out_fd = mkstemp(outfile);
    if (out_fd < 0)
    {
        neo4j_log_error(logger, "Failed to open temp file '%s': %s", outfile,
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        outfile[0] = '\0';
        goto failure;
    }

    out_stream = fdopen(out_fd, "w");
    if (out_stream == NULL)
    {
        neo4j_log_error(logger, "fdopen failed: %s",
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        goto failure;
    }
    out_fd = -1;

    if (in_stream != NULL)
    {
        char line[NEO4J_MAX_KNOWN_HOSTS_LINE_LENGTH];
        size_t hostlen = strlen(host);
        while (fgets(line, sizeof(line), in_stream) != NULL)
        {
            if (strncmp(line, host, hostlen) == 0 && isspace(line[hostlen]))
            {
                continue;
            }

            if (fputs(line, out_stream) == EOF)
            {
                neo4j_log_error(logger, "write failed: %s",
                        neo4j_strerror(errno, ebuf, sizeof(ebuf)));
                goto failure;
            }
        }

        if (!feof(in_stream))
        {
            errno = ferror(in_stream);
            neo4j_log_error(logger, "Failed reading '%s': %s", file,
                    neo4j_strerror(errno, ebuf, sizeof(ebuf)));
            goto failure;
        }

        if (fclose(in_stream))
        {
            neo4j_log_error(logger, "Failed reading '%s': %s", file,
                    neo4j_strerror(errno, ebuf, sizeof(ebuf)));
            goto failure;
        }
        in_stream = NULL;
    }

    if (fprintf(out_stream, "%s %s\n", host, fingerprint) < 0)
    {
        neo4j_log_error(logger, "write failed: %s",
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        goto failure;
    }

    if (fclose(out_stream))
    {
        neo4j_log_error(logger, "write failed: %s",
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        goto failure;
    }
    out_stream = NULL;


    if (rename(outfile, file))
    {
        neo4j_log_error(logger, "rename failed: %s",
                neo4j_strerror(errno, ebuf, sizeof(ebuf)));
        goto failure;
    }

    return 0;

    int errsv;
failure:
    errsv = errno;
    if (in_stream != NULL)
    {
        fclose(in_stream);
    }
    if (out_stream != NULL)
    {
        fclose(out_stream);
    }
    if (out_fd >= 0)
    {
        close(out_fd);
    }
    if (outfile[0] != '\0')
    {
        unlink(outfile);
    }
    errno = errsv;
    return -1;
}