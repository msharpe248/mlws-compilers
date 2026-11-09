-module(application).
-export([get_env/2, get_env/3, set_env/1, set_env/2, set_env/3, set_env/4,
         unset_env/2, unset_env/3, get_all_env/1]).

%% Application environments, on MLWSVM's environments (env.*): App's Key is
%% the VM's value named App.Key, which every process of the program sees at
%% once, and which `mlwsvm run --env FILE` fills from lines of
%% `App.Key = JSON` (a string is an atom, an array a list, an object a map
%% with atom keys). A value set here reads back as the term it was.

get_env(App, Key) -> '$rt':env_get(name(App, Key)).

get_env(App, Key, Default) ->
    case get_env(App, Key) of
        {ok, V} -> V;
        undefined -> Default
    end.

set_env(App, Key, Val) -> '$rt':env_set(name(App, Key), Val).
set_env(App, Key, Val, _Opts) -> set_env(App, Key, Val).

set_env(Config) -> set_env(Config, []).
set_env(Config, _Opts) ->
    [set_env(App, K, V) || {App, Env} <- Config, {K, V} <- Env],
    ok.

unset_env(App, Key) -> '$rt':env_unset(name(App, Key)).
unset_env(App, Key, _Opts) -> unset_env(App, Key).

get_all_env(App) ->
    Prefix = atom_to_list(App) ++ ".",
    N = length(Prefix),
    [{K, V} || Name <- lists:sort('$rt':env_keys()),
               S <- [atom_to_list(Name)],
               lists:prefix(Prefix, S),
               K <- [list_to_atom(lists:nthtail(N, S))],
               {ok, V} <- [get_env(App, K)]].

name(App, Key) when is_atom(App), is_atom(Key) ->
    list_to_atom(atom_to_list(App) ++ "." ++ atom_to_list(Key)).
