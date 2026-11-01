%% timer for mlws-vm-erlc.
-module(timer).
-export([sleep/1, tc/1, tc/2, tc/3, send_after/2, send_after/3, apply_after/4,
         seconds/1, minutes/1, hours/1, hms/3]).

sleep(T) -> '$rt':sleep(T).

tc(F) -> T0 = erlang:monotonic_time(), R = F(), {(erlang:monotonic_time() - T0) div 1000, R}.
tc(F, A) -> T0 = erlang:monotonic_time(), R = apply(F, A), {(erlang:monotonic_time() - T0) div 1000, R}.
tc(M, F, A) -> T0 = erlang:monotonic_time(), R = apply(M, F, A), {(erlang:monotonic_time() - T0) div 1000, R}.

send_after(T, Msg) -> send_after(T, self(), Msg).
send_after(T, Dest, Msg) ->
    spawn(fun() -> '$rt':sleep(T), Dest ! Msg end),
    {ok, make_ref()}.

apply_after(T, M, F, A) ->
    spawn(fun() -> '$rt':sleep(T), apply(M, F, A) end),
    {ok, make_ref()}.

seconds(S) -> S * 1000.
minutes(M) -> M * 60000.
hours(H) -> H * 3600000.
hms(H, M, S) -> hours(H) + minutes(M) + seconds(S).
