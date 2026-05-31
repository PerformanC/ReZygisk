#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * Security invariant: When a process name is interpolated into a SQL query,
 * the resulting query must not contain unescaped SQL metacharacters that could
 * alter the query's logical structure. Specifically, the process name portion
 * of the query must be properly escaped/sanitized such that SQL injection
 * is not possible.
 *
 * The safe approach requires that any double-quote characters in the process
 * name are escaped (doubled) before interpolation, and that the resulting
 * query structure remains semantically equivalent to the intended query.
 */

/* Simulate the vulnerable interpolation */
static void build_query_vulnerable(const char *process, char *out, size_t out_size) {
    snprintf(out, out_size,
             "SELECT 1 FROM denylist WHERE \"%s\" LIKE process || '%%' LIMIT 1",
             process);
}

/* Safe interpolation: escape double-quotes by doubling them */
static void build_query_safe(const char *process, char *out, size_t out_size) {
    char escaped[1024] = {0};
    size_t j = 0;
    for (size_t i = 0; process[i] != '\0' && j < sizeof(escaped) - 2; i++) {
        if (process[i] == '"') {
            escaped[j++] = '"';
            escaped[j++] = '"';
        } else {
            escaped[j++] = process[i];
        }
    }
    escaped[j] = '\0';
    snprintf(out, out_size,
             "SELECT 1 FROM denylist WHERE \"%s\" LIKE process || '%%' LIMIT 1",
             escaped);
}

/*
 * Check if a query has been structurally altered by SQL injection.
 * We verify that the query does not contain unbalanced SQL structural elements
 * that would indicate injection occurred.
 *
 * Specifically, after the opening quote of the process name field, we should
 * only see the process name content followed by a closing quote, not additional
 * SQL keywords or operators that break the intended structure.
 */
static int query_is_structurally_safe(const char *query) {
    /* The query should start with the expected prefix */
    const char *expected_prefix = "SELECT 1 FROM denylist WHERE \"";
    size_t prefix_len = strlen(expected_prefix);

    if (strncmp(query, expected_prefix, prefix_len) != 0) {
        return 0; /* Prefix altered - injection occurred */
    }

    /* After the prefix, find the closing double-quote of the process field.
     * In a safe query, the process name is enclosed in double-quotes.
     * SQL injection via double-quote would break out of this enclosure
     * and alter the query structure.
     *
     * We check: after the opening quote, count unescaped closing quotes.
     * If we find an unescaped closing quote before the expected position,
     * the query structure has been broken.
     */
    const char *process_start = query + prefix_len;

    /* Find the end of the process name field (closing unescaped double-quote) */
    int found_close = 0;
    size_t i = 0;
    while (process_start[i] != '\0') {
        if (process_start[i] == '"') {
            /* Check if it's an escaped double-quote (doubled) */
            if (process_start[i + 1] == '"') {
                i += 2; /* Skip escaped quote */
                continue;
            } else {
                /* This is the closing quote of the identifier */
                found_close = 1;
                i++;
                break;
            }
        }
        i++;
    }

    if (!found_close) {
        return 0; /* No closing quote found - malformed */
    }

    /* After the closing quote, the remainder must match the expected suffix */
    const char *expected_suffix = " LIKE process || '%' LIMIT 1";
    const char *remainder = process_start + i;

    if (strcmp(remainder, expected_suffix) != 0) {
        return 0; /* Suffix altered - injection occurred */
    }

    return 1; /* Query structure is intact */
}

/*
 * Check that a process name does not contain unescaped SQL metacharacters
 * that would be dangerous when interpolated.
 */
static int process_name_is_safe_for_interpolation(const char *process) {
    /* A process name is safe if it contains no unescaped double-quotes
     * (since the vulnerable code uses double-quote delimiters) */
    return strchr(process, '"') == NULL;
}

START_TEST(test_sql_injection_invariant)
{
    /* Invariant: The SQL query built from a process name must always maintain
     * its intended structure. SQL metacharacters in the process name must not
     * be able to alter the query's logical structure. */
    const char *payloads[] = {
        /* SQL injection via double-quote escape */
        "com.evil\" OR 1=1--",
        "com.evil\" OR \"1\"=\"1",
        "com.evil\"; DROP TABLE denylist;--",
        "com.evil\" UNION SELECT 1--",
        "com.evil\" OR 1=1 LIMIT 1--",
        /* Single quote injections */
        "com.evil' OR '1'='1",
        "com.evil'; DROP TABLE denylist;--",
        "com.evil' UNION SELECT 1--",
        /* Combined metacharacters */
        "com.evil\"'",
        "com.evil'\"",
        /* Null byte and special chars */
        "com.evil\x00injected",
        /* Whitespace manipulation */
        "com.evil\" \t OR \t 1=1--",
        /* Comment sequences */
        "com.evil\"/*comment*/OR 1=1--",
        "com.evil\"--",
        /* Nested quotes */
        "com.evil\"\"OR 1=1--",
        /* Unicode/encoded attempts */
        "com.evil\xE2\x80\x9C injected",
        /* Legitimate process names (should pass) */
        "com.example.app",
        "com.google.android.gms",
        "com.android.settings",
        /* Edge cases */
        "",
        "a",
        "com.app.with.many.dots",
        /* Long process name */
        "com.verylongpackagename.that.goes.on.and.on.and.on.app",
        /* Process name with numbers */
        "com.app123.test456",
        /* Semicolon injection */
        "com.evil; DROP TABLE denylist;",
        /* Percent sign (LIKE wildcard) */
        "com.evil%",
        "com.evil%%",
        "%com.evil%",
        /* Underscore (LIKE single char wildcard) */
        "com.evil_",
        "com._evil",
        /* Backslash */
        "com.evil\\",
        "com.evil\\\"",
        /* Mixed attack vectors */
        "\" OR 1=1--",
        "' OR '1'='1'--",
        "\" OR \"\"=\"",
        "1\" OR \"1\"=\"1",
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        char safe_query[2048] = {0};
        char vuln_query[2048] = {0};

        build_query_safe(payloads[i], safe_query, sizeof(safe_query));
        build_query_vulnerable(payloads[i], vuln_query, sizeof(vuln_query));

        /* INVARIANT 1: The safe query must always maintain structural integrity */
        ck_assert_msg(query_is_structurally_safe(safe_query),
                      "Safe query builder produced structurally unsafe query for payload: '%s'\nQuery: %s",
                      payloads[i], safe_query);

        /* INVARIANT 2: If the process name contains SQL metacharacters (double-quotes),
         * the vulnerable interpolation MUST NOT produce a structurally safe query
         * (demonstrating the vulnerability exists and safe version is needed),
         * OR the safe version must differ from the vulnerable version */
        if (strchr(payloads[i], '"') != NULL) {
            /* For payloads with double-quotes: safe and vulnerable queries must differ,
             * proving that escaping is necessary and effective */
            ck_assert_msg(strcmp(safe_query, vuln_query) != 0,
                          "Safe and vulnerable queries are identical for payload with double-quotes: '%s'",
                          payloads[i]);
        }

        /* INVARIANT 3: The safe query must always begin with the correct prefix */
        const char *expected_prefix = "SELECT 1 FROM denylist WHERE \"";
        ck_assert_msg(strncmp(safe_query, expected_prefix, strlen(expected_prefix)) == 0,
                      "Safe query has wrong prefix for payload: '%s'\nQuery: %s",
                      payloads[i], safe_query);

        /* INVARIANT 4: The safe query must always end with the correct suffix */
        const char *expected_suffix = " LIKE process || '%' LIMIT 1";
        size_t query_len = strlen(safe_query);
        size_t suffix_len = strlen(expected_suffix);
        if (query_len >= suffix_len) {
            ck_assert_msg(strcmp(safe_query + query_len - suffix_len, expected_suffix) == 0,
                          "Safe query has wrong suffix for payload: '%s'\nQuery: %s",
                          payloads[i], safe_query);
        }

        /* INVARIANT 5: For process names without double-quotes,
         * both safe and vulnerable queries should be identical
         * (no unnecessary transformation) */
        if (process_name_is_safe_for_interpolation(payloads[i])) {
            ck_assert_msg(strcmp(safe_query, vuln_query) == 0,
                          "Safe and vulnerable queries differ for safe payload: '%s'\nSafe: %s\nVuln: %s",
                          payloads[i], safe_query, vuln_query);
        }
    }
}
END_TEST

START_TEST(test_no_sql_keywords_injected)
{
    /* Invariant: SQL injection keywords must not appear outside the process name
     * field in the constructed query */
    const char *injection_payloads[] = {
        "com.evil\" OR 1=1--",
        "com.evil\" UNION SELECT * FROM sqlite_master--",
        "com.evil\"; DELETE FROM denylist;--",
        "com.evil\" AND 1=2 UNION SELECT name FROM sqlite_master--",
        "com.evil\" OR EXISTS(SELECT 1)--",
        "com.evil\" OR (SELECT COUNT(*) FROM denylist)>0--",
    };
    int num_payloads = sizeof(injection_payloads) / sizeof(injection_payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        char safe_query[2048] = {0};
        build_query_safe(injection_payloads[i], safe_query, sizeof(safe_query));

        /* The safe query must be structurally intact */
        ck_assert_msg(query_is_structurally_safe(safe_query),
                      "SQL injection keywords escaped into query structure for payload: '%s'\nQuery: %s",
                      injection_payloads[i], safe_query);

        /* Verify the query doesn't contain UNION, DELETE, DROP outside quotes */
        /* After proper escaping, these keywords should be inside the quoted identifier */
        const char *dangerous_patterns[] = {
            " UNION ", " DELETE ", " DROP ", " INSERT ", " UPDATE ",
            " OR 1=1", " AND 1=1", "--", "/*"
        };
        int num_patterns = sizeof(dangerous_patterns) / sizeof(dangerous_patterns[0]);

        /* Find where the process name field ends in the safe query */
        const char *prefix = "SELECT 1 FROM denylist WHERE \"";
        size_t prefix_len = strlen(prefix);
        const char *after_prefix = safe_query + prefix_len;

        /* Find the closing quote (accounting for escaped quotes) */
        size_t j = 0;
        while (after_prefix[j] != '\0') {
            if (after_prefix[j] == '"' && after_prefix[j+1] != '"') {
                j++;
                break;
            } else if (after_prefix[j] == '"' && after_prefix[j+1] == '"') {
                j += 2;
            } else {
                j++;
            }
        }

        /* Check the remainder (after the closing quote) for dangerous patterns */
        const char *remainder = after_prefix + j;
        for (int k = 0; k < num_patterns; k++) {
            ck_assert_msg(strstr(remainder, dangerous_patterns[k]) == NULL,
                          "Dangerous SQL pattern '%s' found in query remainder for payload: '%s'\nRemainder: %s",
                          dangerous_patterns[k], injection_payloads[i], remainder);
        }
    }
}
END_TEST

START_TEST(test_boundary_process_names)
{
    /* Invariant: Boundary and edge case process names must be handled safely */
    struct {
        const char *name;
        const char *description;
    } boundary_cases[] = {
        {"", "empty string"},
        {"a", "single character"},
        {"\"", "single double-quote"},
        {"\"\"", "two double-quotes"},
        {"'", "single quote"},
        {"''", "two single quotes"},
        {"\\", "backslash"},
        {"\t", "tab character"},
        {"\n", "newline character"},
        {"com.app", "normal app name"},
        {"com.app.with.\"quotes\".inside", "quotes in middle"},
        {"\"leading.quote", "leading double-quote"},
        {"trailing.quote\"", "trailing double-quote"},
    };
    int num_cases = sizeof(boundary_cases) / sizeof(boundary_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        char safe_query[2048] = {0};
        build_query_safe(boundary_cases[i].name, safe_query, sizeof(safe_query));

        /* Every boundary case must produce a structurally safe query */
        ck_assert_msg(query_is_structurally_safe(safe_query),
                      "Boundary case '%s' (%s) produced unsafe query: %s",
                      boundary_cases[i].name,
                      boundary_cases[i].description,
                      safe_query);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_sql_injection_invariant);
    tcase_add_test(tc_core, test_no_sql_keywords_injected);
    tcase_add_test(tc_core, test_boundary_process_names);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_