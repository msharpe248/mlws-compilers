-module(net_adm).
-export([ping/1, localhost/0]).

ping(Node) when is_atom(Node) ->
    case erts:connect(Node) of
        true -> pong;
        false -> pang
    end.

localhost() -> string:trim('$rt':hostname()).
