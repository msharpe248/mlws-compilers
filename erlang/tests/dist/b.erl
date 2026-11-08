-module(b).
-export([main/1, square/1]).

%% Node b: an echo server, registered, until a tells it to stop.
main(_) ->
    %% registered before b is a node, so that a finds them once it can connect
    register(echo, spawn(fun echo/0)),
    register(b_main, self()),
    erlang:set_cookie(dist_test),
    {ok, _} = net_kernel:start(b@localhost, #{}),
    receive stop -> ok end.

echo() ->
    receive
        {From, Msg} -> From ! {echoed, node(), Msg}, echo()
    end.

square(X) -> X * X.
