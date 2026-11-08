-module(main).
-export([main/1]).

%% Hot code loading: a new version of counter replaces the old while
%% processes run it. A fully qualified call (counter:loop) moves to the new
%% version; a local call stays on the old one, until code:purge kills the
%% process still in it.
main([Dir]) ->
    Q = counter:start(),            % a server
    L = counter:start_local(),      % one that loops by a local call
    MRef = monitor(process, L),
    io:format("before: ~p ~p ~p~n", [counter:version(), ask(Q), ask(L)]),
    {ok, Bin} = file:read_file(Dir ++ "/v2/counter.masm"),
    io:format("load: ~p~n", [code:load_binary(counter, "counter.masm", Bin)]),
    io:format("new calls: ~p~n", [counter:version()]),
    %% Q was waiting in the old loop: it answers from the old code, then
    %% calls counter:loop, the new one
    io:format("server: ~p then ~p~n", [ask(Q), ask(Q)]),
    io:format("local: ~p then ~p~n", [ask(L), ask(L)]),
    io:format("soft_purge: ~p~n", [code:soft_purge(counter)]),
    io:format("purge: ~p~n", [code:purge(counter)]),
    receive {'DOWN', MRef, process, L, Why} -> io:format("local loop: ~p~n", [Why]) end,
    io:format("soft_purge: ~p~n", [code:soft_purge(counter)]),
    io:format("server: ~p~n", [ask(Q)]),
    io:format("is_loaded: ~p, other: ~p~n", [code:is_loaded(counter), code:load_file(nosuch)]),
    Q ! stop.

ask(P) ->
    P ! {get, self()},
    receive {count, N, V} -> {N, V} end.
