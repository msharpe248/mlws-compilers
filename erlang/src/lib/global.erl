-module(global).
-export([register_name/2, register_name/3, re_register_name/2, re_register_name/3,
         unregister_name/1, whereis_name/1, send/2, registered_names/0]).

%% Names across the cluster, on MLWSVM's std.node (its register_global and
%% the rest). Each node keeps every name; a registration is made only if every
%% node connected agrees, and a name goes when its process ends or its node
%% does. When two nodes that each have a name for a different process connect,
%% the lower pid keeps it and the other process is killed, as global's default
%% resolver does. Names are atoms, and this VM must be a node
%% (net_kernel:start).

register_name(Name, Pid) when is_pid(Pid) -> '$rt':global_register(Name, Pid).
register_name(Name, Pid, _Resolve) -> register_name(Name, Pid).

re_register_name(Name, Pid) when is_pid(Pid) ->
    unregister_name(Name),
    yes = register_name(Name, Pid).
re_register_name(Name, Pid, _Resolve) -> re_register_name(Name, Pid).

unregister_name(Name) -> '$rt':global_unregister(Name).

whereis_name(Name) -> '$rt':global_whereis(Name).

send(Name, Msg) ->
    case whereis_name(Name) of
        undefined -> exit({badarg, {Name, Msg}});
        Pid -> Pid ! Msg, Pid
    end.

registered_names() -> '$rt':global_names().
