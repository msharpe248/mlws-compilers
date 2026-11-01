%% The n-body benchmark, with floats in tuples.
-module(nbody).
-export([main/0]).

-define(PI, 3.141592653589793).
-define(SOLAR_MASS, (4 * ?PI * ?PI)).
-define(DAYS, 365.24).

bodies() ->
    [{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, ?SOLAR_MASS},
     {4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
      1.66007664274403694e-03 * ?DAYS, 7.69901118419740425e-03 * ?DAYS,
      -6.90460016972063023e-05 * ?DAYS, 9.54791938424326609e-04 * ?SOLAR_MASS},
     {8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
      -2.76742510726862411e-03 * ?DAYS, 4.99852801234917238e-03 * ?DAYS,
      2.30417297573763929e-05 * ?DAYS, 2.85885980666130812e-04 * ?SOLAR_MASS},
     {1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
      2.96460137564761618e-03 * ?DAYS, 2.37847173959480950e-03 * ?DAYS,
      -2.96589568540237556e-05 * ?DAYS, 4.36624404335156298e-05 * ?SOLAR_MASS},
     {1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
      2.68067772490389322e-03 * ?DAYS, 1.62824170038242295e-03 * ?DAYS,
      -9.51592254519715870e-05 * ?DAYS, 5.15138902046611451e-05 * ?SOLAR_MASS}].

offset(Bs) ->
    {Px, Py, Pz} = lists:foldl(fun({_, _, _, Vx, Vy, Vz, M}, {X, Y, Z}) ->
                                       {X + Vx * M, Y + Vy * M, Z + Vz * M}
                               end, {0.0, 0.0, 0.0}, Bs),
    [{X, Y, Z, _, _, _, M} | Rest] = Bs,
    [{X, Y, Z, -Px / ?SOLAR_MASS, -Py / ?SOLAR_MASS, -Pz / ?SOLAR_MASS, M} | Rest].

energy([]) -> 0.0;
energy([{X, Y, Z, Vx, Vy, Vz, M} | Rest]) ->
    E = 0.5 * M * (Vx * Vx + Vy * Vy + Vz * Vz),
    E - lists:foldl(fun({X2, Y2, Z2, _, _, _, M2}, Acc) ->
                            Dx = X - X2, Dy = Y - Y2, Dz = Z - Z2,
                            Acc + M * M2 / math:sqrt(Dx * Dx + Dy * Dy + Dz * Dz)
                    end, 0.0, Rest) + energy(Rest).

advance(Bs, Dt) ->
    Moved = velocities(Bs, Dt),
    [{X + Dt * Vx, Y + Dt * Vy, Z + Dt * Vz, Vx, Vy, Vz, M} || {X, Y, Z, Vx, Vy, Vz, M} <- Moved].

velocities([], _) -> [];
velocities([B | Rest], Dt) ->
    {B1, Rest1} = pairs(B, Rest, Dt, []),
    [B1 | velocities(Rest1, Dt)].

pairs(B, [], _, Acc) -> {B, lists:reverse(Acc)};
pairs({X, Y, Z, Vx, Vy, Vz, M}, [{X2, Y2, Z2, Vx2, Vy2, Vz2, M2} | Rest], Dt, Acc) ->
    Dx = X - X2, Dy = Y - Y2, Dz = Z - Z2,
    D2 = Dx * Dx + Dy * Dy + Dz * Dz,
    Mag = Dt / (D2 * math:sqrt(D2)),
    B = {X, Y, Z, Vx - Dx * M2 * Mag, Vy - Dy * M2 * Mag, Vz - Dz * M2 * Mag, M},
    B2 = {X2, Y2, Z2, Vx2 + Dx * M * Mag, Vy2 + Dy * M * Mag, Vz2 + Dz * M * Mag, M2},
    pairs(B, Rest, Dt, [B2 | Acc]).

run(Bs, 0) -> Bs;
run(Bs, N) -> run(advance(Bs, 0.01), N - 1).

main() ->
    Bs = offset(bodies()),
    io:format("~.9f~n", [energy(Bs)]),
    io:format("~.9f~n", [energy(run(Bs, 100000))]).
