-module(main).
-export([main/1]).

%% Loading the same code again: the version it replaces is old code all the
%% same, and the process left in it is killed by code:purge. (Nothing of
%% the old version is kept for main by the funs it cached: .reload fresh.)
%% Atoms made at run time are the program's own (text.intern).
main(_) ->
    L = worker:start(),
    MRef = monitor(process, L),
    io:format("~p~n", [worker:ask(L)]),
    io:format("~p~n", [code:load_file(worker)]),
    io:format("~p~n", [code:soft_purge(worker)]),
    io:format("~p~n", [code:purge(worker)]),
    receive {'DOWN', MRef, process, L, Why} -> io:format("~p~n", [Why]) end,
    L2 = worker:start(),
    io:format("~p~n", [worker:ask(L2)]),
    io:format("~p~n", [worker:kind(list_to_atom("pong"))]),
    io:format("~p~n", [worker:kind(binary_to_atom(<<"ping">>, utf8))]).
