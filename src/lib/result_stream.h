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
#ifndef NEO4J_RESULT_STREAM_H
#define NEO4J_RESULT_STREAM_H

#include "neo4j-client.h"


struct neo4j_result_stream
{
    /**
     * Check if a results stream has failed.
     *
     * @param [self] This result stream.
     * @return 0 if no failure has occurred, and an error number otherwise.
     */
    int (*check_failure)(neo4j_result_stream_t *self);

    /**
     * Return the error code sent from Neo4j.
     *
     * When `neo4j_check_failure` returns `NEO4J_STATEMENT_EVALUATION_FAILED`,
     * then this function can be used to get the error code sent from Neo4j.
     *
     * @param [self] This result stream.
     * @return A `NULL` terminated string reprenting the error code, or NULL
     *         if the stream has not failed or the failure was not
     *         `NEO4J_STATEMENT_EVALUATION_FAILED`.
     */
    const char *(*error_code)(neo4j_result_stream_t *self);

    /**
     * Return the error message sent from Neo4j.
     *
     * When `neo4j_check_failure` returns `NEO4J_STATEMENT_EVALUATION_FAILED`,
     * then this function can be used to get the detailed error message sent
     * from Neo4j.
     *
     * @param [self] This result stream.
     * @return A `NULL` terminated string containing the error message, or NULL
     *         if the stream has not failed or the failure was not
     *         `NEO4J_STATEMENT_EVALUATION_FAILED`.
     */
    const char *(*error_message)(neo4j_result_stream_t *self);

    /**
     * Get the number of fields in a result stream.
     *
     * @param [self] This result stream.
     * @return The number of fields in the result, or -1 on failure
     *         (errno will be set).
     */
    unsigned int (*nfields)(neo4j_result_stream_t *self);

    /**
     * Get the name of a field in a result stream.
     *
     * @param [self] This result stream.
     * @param [index] The field index to get the name of.
     * @return The name of the field, as a NULL terminated string,
     *         or NULL if an error occurs (errno will be set).
     */
    const char *(*fieldname)(neo4j_result_stream_t *self, unsigned int index);

    /**
     * Fetch the next record from the result stream.
     *
     * @param [self] This result stream.
     * @return The next result, or NULL if the stream is exahusted or an
     *         error has occurred (errno will be set).
     */
    neo4j_result_t *(*fetch_next)(neo4j_result_stream_t *self);

    /**
     * Close a result stream.
     *
     * Closes the result stream and releases all memory held by it, including
     * results and values obtained from it.
     *
     * NOTE: After this function is invoked, all `neo4j_result_t` objects
     * fetched from this stream, and any values obtained from them, will be
     * invalid and _must not be accessed_. Doing so will result in undetermined
     * and unstable behaviour. This is true even if this function returns an
     * error.
     *
     * @param [self] This result stream. The pointer will be invalid after the
     *         function returns.
     * @return 0 on success, or -1 on failure (errno will be set).
     */
    int (*close)(neo4j_result_stream_t *self);
};


struct neo4j_result
{
    /**
     * Get a field from a result.
     *
     * @param [self] This result.
     * @param [index] The field index to get.
     * @return The field from the result, or `neo4j_null` if index
     *         is out of bounds.
     */
    neo4j_value_t (*field)(const neo4j_result_t *self, unsigned int index);

    /**
     * Retain a result.
     *
     * This retains the result and all values contained within it, preventing
     * them from being deallocated on the next call to `neo4j_fetch_next(...)`
     * or when the result stream is closed via `neo4j_close_results(...)`. Once
     * retained, the result _must_ later be explicitly released via
     * `neo4j_release(...)`.
     *
     * @param [result] This result.
     * @return The result.
     */
    neo4j_result_t *(*retain)(neo4j_result_t *self);

    /**
     * Release a result.
     *
     * @param [result] This result.
     */
    void (*release)(neo4j_result_t *self);
};


#endif/*NEO4J_RESULT_STREAM_H*/