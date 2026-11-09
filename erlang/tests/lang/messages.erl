-module(messages).
-export([main/0]).

%% Every kind of term as a message, there and back, and receives that skip
%% some: integers and floats travel as numbers, other terms as objects.
main() ->
    P = spawn(fun echo/0),
    Ts = [{0, 0}, {-1, -1}, {big, 1 bsl 62}, {bigger, 1 bsl 100}, {1.5, 1.5},
          {'-0.0', -0.0}, {'0.0', 0.0}, {nil, []}, {foo, foo}, {str, "str"},
          {'{}', {}}, {pair, {1, 2.5}}, {improper, [1 | 2]}, {map, #{a => 1}},
          {bin, <<1, 2>>}, {'fun', fun() -> ok end}, {pid, self()},
          {ref, make_ref()}, {nils, {[], []}}, {huge, 3.0e300}],
    [begin
         P ! {self(), T},
         receive {echo, T2} -> io:format("~p ~p~n", [Name, T2 =:= T]) end
     end || {Name, T} <- Ts],
    self() ! 2.5, self() ! 7, self() ! [], self() ! x,
    receive x -> io:format("x first~n") end,
    receive [] -> io:format("[] next~n") end,
    receive F when is_float(F) -> io:format("float ~p~n", [F]) end,
    receive I -> io:format("int ~p~n", [I]) end,
    receive _ -> no after 10 -> io:format("empty~n") end,
    self() ! 1.25,
    receive Z -> io:format("~p ~p~n", [Z, is_float(Z)]) after 100 -> io:format("lost~n") end,
    P ! 3,
    P ! {self(), done},
    receive {echo, done} -> io:format("an int it did not take: skipped~n") end.

echo() ->
    receive
        {From, T} -> From ! {echo, T}, echo()
    end.
