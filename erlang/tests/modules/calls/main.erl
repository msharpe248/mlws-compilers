-module(main).
-export([main/1, back/1]).

%% Calls between modules compiled with --modules, each its own MLWSVM
%% module: qualified calls, calls back (helper calls main, which calls
%% helper: no import cycle), apply/3, external funs, spawn/3, undef, and
%% code:load_file of a new version written over the old file.
main([Dir]) ->
    io:format("~p~n", [helper:double(21)]),
    io:format("~p~n", [helper:twice_back(5)]),
    io:format("~p~n", [apply(helper, double, [5])]),
    F = fun helper:double/1,
    io:format("~p~n", [lists:map(F, [1, 2, 3])]),
    io:format("~p~n", [catch helper:nope(1)]),
    io:format("~p~n", [catch nosuch:f()]),
    Me = self(),
    spawn(helper, tell, [Me, hello]),
    receive {told, M} -> io:format("~p~n", [M]) end,
    io:format("~p~n", [code:is_loaded(helper)]),
    {ok, New} = file:read_file(Dir ++ "/v2/helper.masm"),
    ok = file:write_file(Dir ++ "/helper.masm", New),
    io:format("~p~n", [code:load_file(helper)]),
    io:format("~p ~p~n", [helper:double(21), F(21)]),
    io:format("~p~n", [code:load_file(nosuch)]).

back(X) -> X + 1.
