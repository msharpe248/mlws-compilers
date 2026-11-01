-module(binaries).
-export([main/0]).

parse(<<Len:8, Data:Len/binary, Rest/binary>>) -> [Data | parse(Rest)];
parse(<<>>) -> [].

sum_bytes(<<B, Rest/binary>>, Acc) -> sum_bytes(Rest, Acc + B);
sum_bytes(<<>>, Acc) -> Acc.

main() ->
    B = <<1, 2, 3>>,
    io:format("~p ~p~n", [B, byte_size(B)]),
    io:format("~p~n", [<<"hello">>]),
    io:format("~p~n", [binary_to_list(<<"abc">>)]),
    io:format("~p~n", [list_to_binary([<<"ab">>, "cd", $e])]),
    <<X:16, Y:8>> = <<1, 2, 3>>,
    io:format("~p ~p~n", [X, Y]),
    <<A:16/little>> = <<1, 2>>,
    io:format("~p~n", [A]),
    io:format("~p~n", [<<256:16, 255:8, -1:8>>]),
    io:format("~p~n", [parse(<<2, "ab", 3, "cde">>)]),
    io:format("~p~n", [sum_bytes(<<1, 2, 3, 4>>, 0)]),
    <<"GET ", Path/binary>> = <<"GET /index.html">>,
    io:format("~p~n", [Path]),
    io:format("~p~n", [<< <<(C + 1)>> || <<C>> <= <<"HAL">> >>]),
    io:format("~p~n", [[C || <<C>> <= <<"abc">>]]),
    io:format("~p~n", [<< <<X1:8>> || X1 <- [65, 66, 67] >>]),
    io:format("~s~n", [<<"as string">>]),
    io:format("~p~n", [binary_part(<<"abcdef">>, 1, 3)]),
    io:format("~p~n", [<<1.5/float>>]),
    <<F:64/float>> = <<1.5/float>>,
    io:format("~p~n", [F]),
    io:format("~p~n", [atom_to_binary(hello)]),
    io:format("~p~n", [binary_to_atom(<<"world">>)]),
    io:format("~p~n", [integer_to_binary(1234)]),
    io:format("~p~n", [<<"abc">> < <<"abd">>]),
    io:format("~p~n", [<<"é"/utf8>>]),
    ok.
