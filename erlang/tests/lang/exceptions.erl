-module(exceptions).
-export([main/0]).

safe_div(A, B) ->
    try A div B of
        Q -> {ok, Q}
    catch
        error:badarith -> {error, badarith}
    end.

thrower(throw) -> throw(my_throw);
thrower(error) -> error(my_error);
thrower(exit) -> exit(my_exit);
thrower(badmatch) -> 1 = 2;
thrower(_) -> fine.

classify(What) ->
    try thrower(What) of
        R -> {returned, R}
    catch
        throw:T -> {caught_throw, T};
        error:{badmatch, V} -> {caught_badmatch, V};
        error:E -> {caught_error, E};
        exit:X -> {caught_exit, X}
    end.

main() ->
    io:format("~p ~p~n", [safe_div(10, 2), safe_div(1, 0)]),
    io:format("~p~n", [[classify(W) || W <- [throw, error, exit, badmatch, none]]]),
    io:format("~p~n", [catch throw(hello)]),
    io:format("~p~n", [catch 1 + 2]),
    {'EXIT', {badarith, _}} = catch 1 / 0,
    io:format("caught badarith~n"),
    {'EXIT', foo} = catch exit(foo),
    io:format("caught exit~n"),
    R = try
            throw(x)
        catch
            Class:Reason -> {Class, Reason}
        after
            io:format("after runs~n")
        end,
    io:format("~p~n", [R]),
    try
        try error(inner)
        after io:format("inner after~n")
        end
    catch error:inner -> io:format("outer caught inner~n")
    end,
    try lists:nth(5, [1]) of
        _ -> no
    catch
        error:function_clause -> io:format("function_clause~n")
    end,
    try case 3 of 1 -> a end
    catch error:{case_clause, 3} -> io:format("case_clause~n")
    end,
    try element(5, {a}) catch error:badarg -> io:format("badarg~n") end,
    try atom + 1 catch error:badarith -> io:format("badarith~n") end,
    Result = try 42 catch _:_ -> no end,
    io:format("~p~n", [Result]),
    ok.
