import subprocess
import textwrap
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CODEX_SRC = REPO_ROOT / "applications" / "codex_keys" / "src"

# codex_json.c is deliberately Zephyr-free plain C so the request parser can be
# exercised on the host: the interesting case (a nested "id" inside params must
# not shadow the request id) is awkward to reproduce on hardware.
HARNESS = """
    #include <assert.h>
    #include <stdio.h>
    #include <string.h>

    #include "codex_json.h"

    static void expect(const char *json, const char *key, const char *want)
    {
        size_t len = 0;
        const char *val = codex_json_member(json, key, &len);

        if (want == NULL) {
            assert(val == NULL);
            return;
        }
        assert(val != NULL);
        assert(len == strlen(want));
        assert(memcmp(val, want, len) == 0);
    }

    int main(void)
    {
        const char *status =
            "{\\"method\\":\\"v.oai.thstatus\\","
            "\\"params\\":[{\\"id\\":0,\\"c\\":16711680,\\"e\\":\\"breath\\"}],\\"id\\":7}";

        /* The id nested in params must not shadow the request id. */
        expect(status, "id", "7");
        expect(status, "method", "\\"v.oai.thstatus\\"");
        expect(status, "params", "[{\\"id\\":0,\\"c\\":16711680,\\"e\\":\\"breath\\"}]");

        /* String ids are echoed verbatim, braces in strings do not confuse it. */
        expect("{\\"method\\":\\"sys.version\\",\\"id\\":\\"a}b\\"}", "id", "\\"a}b\\"");

        /* Absent members and notifications without an id. */
        expect("{\\"method\\":\\"v.oai.hid\\",\\"params\\":{\\"k\\":\\"AG00\\"}}", "id", NULL);
        expect("{}", "method", NULL);

        /* Truncated input must not run off the end. */
        expect("{\\"method\\":\\"sys.ver", "method", NULL);
        expect("{\\"params\\":[{\\"id\\":0}", "id", NULL);

        {
            size_t len = 0;
            const char *m = codex_json_member(status, "method", &len);

            assert(codex_json_str_eq(m, len, "v.oai.thstatus"));
            assert(!codex_json_str_eq(m, len, "v.oai.thstatu"));
            assert(!codex_json_str_eq(m, len, "sys.version"));
        }

        printf("ok\\n");
        return 0;
    }
"""


def test_codex_json_member(tmp_path: Path) -> None:
    test_c = tmp_path / "test_codex_json.c"
    test_c.write_text(textwrap.dedent(HARNESS))
    exe = tmp_path / "test_codex_json"

    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-I",
            str(CODEX_SRC),
            str(test_c),
            str(CODEX_SRC / "codex_json.c"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    assert subprocess.check_output([exe]).strip() == b"ok"
