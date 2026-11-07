-module(regexes).
-export([main/0]).

%% re on MLWSVM's regular expressions (RE2's syntax: no backreferences or
%% lookaround).
main() ->
    P = fun(X) -> io:format("~p~n", [X]) end,
    %% run
    P(re:run("hello world", "o")),
    P(re:run(<<"hello world">>, "w(or)(x)?ld")),
    P(re:run("hello world", "z")),
    P(re:run("hello", "l+", [{capture, none}])),
    P(re:run("abc123def", "[0-9]+", [{capture, first, list}])),
    P(re:run("abc123def", "([a-z]+)([0-9]+)", [{capture, all_but_first, binary}])),
    P(re:run("key=value", "(\\w+)=(\\w+)", [{capture, [2, 1], list}])),
    P(re:run("a1b22c333", "\\d+", [global])),
    P(re:run("a1b22c333", "[a-z](\\d+)", [global, {capture, all_but_first, list}])),
    P(re:run("abc", "x*", [global])),
    P(re:run("Hello", "hello", [caseless, {capture, first, list}])),
    P(re:run("one\ntwo", "^two$", [multiline, {capture, first, list}])),
    P(re:run("a\nb", "a.b", [dotall, {capture, first, list}])),
    P(re:run("aaa", "a", [{offset, 1}, {capture, first, index}])),
    P(re:run("x", "(a)|x", [{capture, all, list}])),
    P(re:run("x", "(a)|x")),
    %% compile
    {ok, MP} = re:compile("^(\\d{4})-(\\d{2})$"),
    P(re:run("2026-11", MP, [{capture, all_but_first, list}])),
    P(re:run("26-11", MP)),
    P(element(1, re:compile("("))),
    P(try re:run("x", "(") catch error:badarg -> badarg end),
    %% replace
    P(re:replace("hello world", "o", "0", [{return, list}])),
    P(re:replace("hello world", "o", "0", [global, {return, list}])),
    P(re:replace("2026-11-07", "(\\d+)-(\\d+)-(\\d+)", "\\3/\\2/\\1", [{return, binary}])),
    P(re:replace("abc", "b", "[&]", [{return, list}])),
    P(re:replace("abc", "b", "\\\\&", [{return, list}])),
    P(re:replace("abc", "b", "\\g{0}\\g{0}", [{return, list}])),
    P(re:replace("a.b.c", "\\.", "$", [global, {return, list}])),
    P(re:replace("aaa", "x*", "-", [global, {return, list}])),
    P(iolist_to_binary(re:replace("abc", "c", "C"))),
    %% split
    P(re:split("a,b,,c,,", ",")),
    P(re:split("a,b,,c,,", ",", [trim])),
    P(re:split("a,b,c,d", ",", [{parts, 2}, {return, list}])),
    P(re:split("a1b2c", "(\\d)", [{return, list}])),
    P(re:split("a1b2c", "(\\d)", [group, {return, list}])),
    P(re:split("abc", "", [{return, list}])),
    P(re:split("  x  y ", "\\s+", [{return, list}, trim])),
    P(re:split("a,b", ",*", [{return, list}])),
    P(re:run("axb", "x*", [global])),
    P(re:replace("a,b", ",*", "-", [global, {return, list}])),
    %% unicode
    P(re:run("héllo", "l+", [unicode])),
    P(re:replace("héllo", "é", "e", [unicode, {return, list}])),
    P(re:split(<<"α,β"/utf8>>, ",", [unicode, {return, list}])),
    %% named groups
    D = "(?<year>\\d+)-(?<mon>\\d+)(x)?(?P<day>\\d+)?",
    P(re:run("2024-11", D, [{capture, all_names, binary}])),
    P(re:run("2024-11", D, [{capture, all_names, index}])),
    P(re:run("2024-11", D, [{capture, [mon, "year", <<"day">>], list}])),
    P(re:run("2024-11", D, [{capture, [mon, 3, nosuch], index}])),
    P(re:run("2024-11 1999-01", D, [global, {capture, [year], list}])),
    P(re:run("2024-11", "(\\d+)", [{capture, all_names}])),
    P(re:run("abc", D, [{capture, all_names}])),
    {ok, DP} = re:compile(D),
    P(re:inspect(DP, namelist)),
    P(re:run("1999-12", DP, [{capture, [day, mon], binary}])),
    ok.
