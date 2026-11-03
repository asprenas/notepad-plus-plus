"""Regular expression compatibility cases (phase 3, criterion "정규식 호환 테스트").

Writes app/tests/data/regex/cases.json. Expected results are NOT computed here: they are
extracted once from the original Windows Notepad++ by tools/regex-compat/extract-from-npp.ps1
(-> expected.json), then the core test (core.regexcompat) compares the port with them.

Usage (from the repository root):  python -I tools/regex-compat/gen-cases.py
"""
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[2]
out = root / "app/tests/data/regex/cases.json"

LOREM = "The quick brown fox jumps over the lazy dog.\r\nTHE QUICK BROWN FOX\r\nfoo123 bar_45 baz-6\r\n\r\nend"
CODE = "int main() {\n    return foo(1, 2) + bar[3];\n}\n// comment here\n/* block\n   comment */\n"
CSV = "id,name,age\n1,Alice,30\n2,Bob,25\n3,\"Smith, J\",40\n"
HTML = '<div class="a"><span id=x>Text</span><br/><a href="http://x.org/a?b=1&c=2">link</a></div>'
KO = "한글 텍스트 테스트\r\n가나다 abc 123\r\nÉcole café naïve\r\n😀 emoji 👍 end"
LOG = "2024-01-15 10:23:45 ERROR [main] Failed\n2024-01-15 10:24:00 INFO  [worker-1] Ok\n2024-02-01 08:00:00 WARN  [main] Slow\n"
PATHS = "C:\\Users\\me\\file.txt\n/home/me/file.txt\n\\\\server\\share\\x\n"
SPACES = "  leading\ntrailing   \n\t tab mixed \t\n\n   \nlast"
DUP = "apple\napple\nbanana\napple\ncherry\ncherry\n"

cases = []


def add(name, find, replace="", text=LOREM, mode="regex", case=False, word=False, dotnl=False, windowsCodepage=False):
    # windowsCodepage: upstream maps the case of non-ASCII letters with the Win32 locale traits of
    # the system ANSI code page (\U on "é" depends on it); compared on Windows only
    cases.append({"name": name, "find": find, "replace": replace, "text": text, "mode": mode,
                  "matchCase": case, "wholeWord": word, "dotMatchesNewline": dotnl,
                  "windowsCodepageDependent": windowsCodepage})


# --- basics -------------------------------------------------------------------------------------
add("literal", "fox", "cat")
add("literal case", "fox", "cat", case=True)
add("ignore case", "the", "a")
add("match case", "THE", "a", case=True)
add("whole word normal", "foo", "x", text="foo foobar barfoo foo_ foo.", mode="normal", word=True)
add("dot", "q.i", "_")
add("dot newline off", "fox.*", "X", text=LOREM)
add("dot newline on", "fox.*?THE", "X", dotnl=True)
add("char class", "[aeiou]", "#")
add("negated class", "[^a-z\\r\\n ]", "_", case=True)
add("range digits", "[0-9]+", "<$0>")
add("shorthand d", "\\d+", "N")
add("shorthand w", "\\w+", "W")
add("shorthand s", "\\s+", " ", text=SPACES)
add("shorthand D", "\\D+", "|", text="a1b22c333")
add("shorthand W", "\\W", "_", text="a-b c.d!e")
add("shorthand S", "\\S+", "S", text=SPACES)
add("h horizontal space", "\\h+", "·", text=SPACES)
add("v vertical space", "\\v", "|", text=SPACES)
add("R any newline", "\\R", "¶", text="a\r\nb\nc\rd")
add("anchors line start", "^", "> ")
add("anchors line end", "$", " <", text=LOREM)
add("anchors both empty line", "^$", "EMPTY", text=SPACES)
add("buffer start", "\\A\\w+", "START")
add("buffer end", "\\w+\\z", "END")
add("buffer end Z", "\\w+\\Z", "END", text="abc def\n")
add("word boundary", "\\bba", "BA", text="bar baz abba ba")
add("non word boundary", "\\Ba", "A", text="bar baz abba ba")
add("word start <", "\\<f", "F", text="foo off fluff")
add("word end >", "o\\>", "0", text="foo off two")
# --- quantifiers --------------------------------------------------------------------------------
add("star greedy", "o.*o", "[]")
add("lazy", "o.*?o", "[]")
add("plus", "o+", "0", text="foo boo o oooo")
add("optional", "colou?r", "hue", text="color colour colouur")
add("interval", "\\d{2,3}", "#", text="1 12 123 1234 12345")
add("interval exact", "\\d{3}", "#", text="1 12 123 1234 12345")
add("interval open", "a{2,}", "A", text="a aa aaa aaaa")
add("possessive", "\\d++\\d", "X", text="12345")
add("atomic group", "(?>\\d+)\\d", "X", text="12345 678")
add("lazy interval", "a{2,4}?", "-", text="aaaaaaa")
# --- groups and back references -----------------------------------------------------------------
add("swap words", "(\\w+) (\\w+)", "$2 $1", text="hello world foo bar")
add("backref \\1 replace", "(\\w+)@(\\w+)", "\\2 at \\1", text="me@home you@work")
add("braced backref", "(\\d)(\\d)", "${2}0${1}", text="12 34 56")
add("whole match $0", "\\d+", "[$0]", text="a1 b22")
add("whole match $&", "\\d+", "($&)", text="a1 b22")
add("backref in pattern", "\\b(\\w+)\\s+\\1\\b", "$1", text="the the cat sat sat on on the mat")
add("named group", "(?<year>\\d{4})-(?<m>\\d\\d)", "$+{m}/$+{year}", text=LOG)
add("non capturing", "(?:ab)+", "X", text="ababab abc ab")
add("alternation", "cat|dog|fox", "pet", text="cat dog fox cow")
add("alternation grouping", "gr(a|e)y", "GR$1Y", text="gray grey griy")
add("nested groups", "((\\w)\\w*)-\\2", "<$1>", text="abc-a xyz-y def-x")
add("optional group missing", "(a)?b", "[$1]", text="b ab")
add("recursion", "\\((?:[^()]|(?R))*\\)", "P", text="f(a(b)c) g((x)) (unbalanced")
add("subroutine", "(\\d+)-(?1)", "N", text="12-34 5-6 x-1")
add("conditional", "(<)?\\w+(?(1)>)", "T", text="<abc> def <gh")
add("branch reset", "(?|(\\d+)|([a-z]+))", "[$1]", text="12 ab 3c")
# --- lookaround -----------------------------------------------------------------------------------
add("lookahead", "\\w+(?=\\.)", "W")
add("negative lookahead", "\\b(?!foo)\\w+\\d", "X")
add("lookbehind", "(?<=\\$)\\d+", "N", text="cost $100 and $25, not 30")
add("negative lookbehind", "(?<!\\$)\\b\\d+", "N", text="cost $100 and $25, not 30")
add("keep out \\K", "name=\\K\\w+", "X", text="name=abc other name=def")
add("lookahead password", "^(?=.*\\d)(?=.*[A-Z]).{6,}$", "OK", text="abc\nAbc123\nabcdef1\nZZ9zzz\n", case=True)
# --- case conversion in replacement ----------------------------------------------------------------
add("upper \\U", "\\b(\\w)(\\w*)", "\\U$1\\E$2", text="hello big world")
add("lower \\L", "[A-Z]+", "\\L$0", text="THE QUICK Brown", case=True)
add("upper first \\u", "\\b\\w+", "\\u$0", text="alpha beta gamma")
add("lower first \\l", "\\b\\w+", "\\l$0", text="Alpha BETA Gamma")
add("upper range then end", "(\\w+)_(\\w+)", "\\U$1\\E_$2", text="foo_bar baz_qux")
add("upper hangul neutral", "(\\w+)", "\\U$1", text="abc 한글 éa", windowsCodepage=True)
# --- inline modifiers ------------------------------------------------------------------------------
add("inline ignore case", "(?i)the", "X", case=True)
add("inline case sensitive", "(?-i)the", "X")
add("inline dotall", "(?s)fox.THE", "X", text="fox\nTHE fox THE")
add("inline multiline off", "(?-m)^\\w+", "X")
add("inline extended", "(?x) \\d + # digits", "N", text="a1 b22")
# --- POSIX classes -------------------------------------------------------------------------------
add("posix alpha", "[[:alpha:]]+", "A", text=KO)
add("posix digit", "[[:digit:]]+", "D", text=KO)
add("posix upper", "[[:upper:]]", "U", text="aBcDÉé", case=True)
add("posix space", "[[:space:]]+", "_", text=SPACES)
add("posix punct", "[[:punct:]]", "", text="a.b,c!d?e")
add("posix alnum", "[[:alnum:]]+", "X", text="ab12 -- cd")
# --- unicode / encoding ----------------------------------------------------------------------------
add("hangul literal", "한글", "Korean", text=KO)
add("hangul class", "[가-힣]+", "K", text=KO)
add("hangul word", "\\w+", "W", text=KO)
add("accented ignore case", "école", "school", text=KO)
add("emoji literal", "😀", ":)", text=KO)
add("emoji dot", "e.", "[$0]", text="e😀 eé ea")
add("hex escape", "\\x{D55C}", "H", text=KO)
add("hex escape short", "\\x41", "a", text="ABCA", case=True)
add("unicode boundary", "\\b\\w", "|", text=KO)
# --- line oriented tasks (community FAQ) ------------------------------------------------------------
add("trim trailing", "[ \\t]+$", "", text=SPACES)
add("trim leading", "^[ \\t]+", "", text=SPACES)
add("empty lines", "^\\R", "", text=SPACES)
add("blank lines", "^[ \\t]*\\R", "", text=SPACES)
add("join lines", "\\R(?=\\w)", " ", text="a\nb\n\nc\nd")
add("duplicate consecutive lines", "^(.*)(\\R\\1)+$", "$1", text=DUP)
add("delete lines containing", "^.*apple.*\\R?", "", text=DUP)
add("keep lines containing", "^(?!.*apple).*\\R?", "", text=DUP)
add("add line numbers style", "^", "- ", text=DUP)
add("csv column swap", "^([^,\\n]*),([^,\\n]*)", "$2,$1", text=CSV)
add("csv quoted field", "\"[^\"]*\"", "Q", text=CSV)
add("log date reformat", "(\\d{4})-(\\d{2})-(\\d{2})", "$3.$2.$1", text=LOG)
add("log level", "\\b(ERROR|WARN)\\b", "<$1>", text=LOG, case=True)
add("log brackets", "\\[([^\\]]+)\\]", "{$1}", text=LOG)
add("html tags", "<[^>]+>", "", text=HTML)
add("html attribute", "(\\w+)=\"([^\"]*)\"", "$1='$2'", text=HTML)
add("url", "https?://[^\\s\"]+", "URL", text=HTML)
add("email", "[\\w.+-]+@[\\w-]+\\.[\\w.]+", "<mail>", text="a.b@c.com, x+y@d-e.org; bad@x")
add("windows paths", "\\\\", "/", text=PATHS)
add("c comments line", "//.*$", "", text=CODE)
add("c comments block", "/\\*.*?\\*/", "", text=CODE, dotnl=True)
add("function call", "(\\w+)\\(([^)]*)\\)", "$1[$2]", text=CODE)
add("indent", "^    ", "\\t", text=CODE)
add("between markers", "(?<=\\{)[^}]*(?=\\})", " ... ", text=CODE)
add("number thousands", "(?<=\\d)(?=(\\d{3})+\\b)", ",", text="1234567 89 1000")
add("hex numbers", "\\b0x[0-9a-fA-F]+\\b", "HEX", text="0x1F 0xGG x0 0xabcDEF")
add("ip address", "\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b", "IP", text="10.0.0.1 256.1.1 192.168.1.255")
add("repeated chars", "(.)\\1+", "$1", text="aaabbbcd  eee")
add("capitalize sentences", "(^|[.!?]\\s+)(\\w)", "$1\\u$2", text="one. two! three? four")
add("wrap in quotes", "^(.+)$", "\"$1\"", text=DUP)
# --- empty matches and special replacements ----------------------------------------------------------
add("empty lookahead insert", "(?=b)", "|", text="abcabc")
add("star empty", "x*", "-", text="axxb")
add("empty alternation", "a|", "#", text="bab")
add("literal dollar in replace", "cost", "\\$5", text="cost cost")
add("escaped backslash replace", "/", "\\\\", text="a/b/c")
add("newline in replace", ",", "\\r\\n", text="a,b,c")
add("tab in replace", " ", "\\t", text="a b c")
add("crlf skip", "\\n", "N", text="a\r\nb\nc")
add("dot vs crlf", "a.", "X", text="a\r\na\nab")
# --- extended mode ---------------------------------------------------------------------------------
add("extended newline", "\\r\\n", " ", mode="extended")
add("extended tab", "\\t", "->", text=SPACES, mode="extended")
add("extended hex", "\\x41", "a", text="ABA", mode="extended", case=True)
add("extended decimal", "\\d065", "a", text="ABA", mode="extended", case=True)
add("extended unicode", "\\u00e9", "e", text="café école", mode="extended")
add("extended replace newline", ";", "\\n", text="a;b;c", mode="extended")
# --- normal mode special characters ------------------------------------------------------------------
add("normal dot literal", ".", "!", mode="normal")
add("normal brackets literal", "[3]", "(3)", text=CODE, mode="normal")
add("normal backslash", "\\", "/", text=PATHS, mode="normal")
add("normal whole word hangul", "한글", "K", text="한글 한글말 한글", mode="normal", word=True)



def read_base(s, base, size):
    temp = 0
    mx = ord('0') + base - 1
    for ch in s[:size]:
        c = ord(ch)
        if c >= ord('A'):
            c &= 0xDF
            c -= ord('A') - ord('0') - 10
        elif c > ord('9'):
            return None
        if ord('0') <= c <= mx:
            temp = temp * base + (c - ord('0'))
        else:
            return None
    return temp


BS = chr(92)


def convert_extended(q):
    """Port of upstream Searching::convertExtendedToString (works on UTF-16 units like upstream)."""
    out, i, n = [], 0, len(q)
    while i < n:
        cur = q[i]
        left = n - i - 1
        if cur == BS and left:
            i += 1
            left -= 1
            cur = q[i]
            simple = {'r': chr(13), 'n': chr(10), '0': chr(0), 't': chr(9), BS: BS}
            if cur in simple:
                out.append(simple[cur])
            elif cur in 'bdoxu':
                size, base = {'b': (8, 2), 'o': (3, 8), 'd': (3, 10), 'x': (2, 16), 'u': (4, 16)}[cur]
                v = read_base(q[i + 1:i + 1 + size], base, size) if left >= size else None
                if v is not None:
                    out.append(chr(v))
                    i += size
                else:
                    out.append(BS + cur)
            else:
                out.append(BS + cur)
        else:
            out.append(cur)
        i += 1
    return ''.join(out)


for c in cases:
    ext = c["mode"] == "extended"
    c["engineFind"] = convert_extended(c["find"]) if ext else c["find"]
    c["engineReplace"] = convert_extended(c["replace"]) if ext else c["replace"]

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(cases, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
print(f"{len(cases)} cases -> {out.relative_to(root)}")
