-module(strings).
-export([main/0]).

main() ->
    io:format("~p~n", [string:tokens("a,b,,c", ",")]),
    io:format("~p~n", [string:join(["x", "y", "z"], "-")]),
    io:format("~p~n", [string:to_upper("hello")]),
    io:format("~p~n", [string:to_lower("HeLLo")]),
    io:format("~p~n", [string:strip("  pad  ")]),
    io:format("~p~n", [string:substr("hello world", 7)]),
    io:format("~p~n", [string:str("hello world", "wor")]),
    io:format("~p~n", [string:split("a=b=c", "=")]),
    io:format("~p~n", [string:split("a=b=c", "=", all)]),
    io:format("~p~n", [string:trim("  both  ")]),
    io:format("~p~n", [string:lexemes("one  two three", " ")]),
    io:format("~p~n", [string:length("four")]),
    io:format("~p~n", [string:find("hello", "ll")]),
    io:format("~p~n", [string:replace("a.b.c", ".", "/", all)]),
    io:format("~p~n", [lists:flatten(string:replace("a.b.c", ".", "/", all))]),
    io:format("~p~n", [string:to_integer("42abc")]),
    io:format("~p~n", [string:copies("ab", 3)]),
    io:format("~p~n", [string:left("ab", 5, $.)]),
    io:format("~p~n", [string:right("ab", 5)]),
    io:format("~p~n", [string:chr("hello", $l)]),
    io:format("~p~n", [lists:reverse("stressed")]),
    io:format("~p~n", ["abc" ++ "def"]),
    io:format("~s~n", [["deep", [$, | " list"]]]),
    ok.
