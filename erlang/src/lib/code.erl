-module(code).
-export([load_file/1, load_binary/3, ensure_loaded/1, purge/1, soft_purge/1, is_loaded/1]).

%% Hot code loading, on MLWSVM's module.load, module.reload,
%% module.reload_text, module.purge and module.purge_kill. A program
%% compiled with --modules has each of its modules as an MLWSVM module of
%% its own, which these load and replace; one compiled to a single file has
%% none, and load_file says {error, embedded}.

load_file(M) when is_atom(M) -> result(M, '$rt':code_load(M));
load_file(_) -> error(badarg).

%% Bin is the module's assembly, as mlws-vm-erlc --modules writes it.
load_binary(M, _File, Bin) when is_atom(M), is_binary(Bin) -> result(M, '$rt':code_load_text(M, Bin));
load_binary(_, _, _) -> error(badarg).

ensure_loaded(M) when is_atom(M) ->
    case '$rt':code_loaded(M) of
        true -> {module, M};
        false -> result(M, '$rt':code_load(M))
    end.

result(M, ok) -> {module, M};
result(_, Why) -> {error, Why}.

%% Frees M's old code, killing the processes still running it: true if it
%% killed any.
purge(M) when is_atom(M) -> '$rt':code_purge(M).

%% Frees M's old code if no process runs it: false if one still does.
soft_purge(M) when is_atom(M) -> '$rt':code_soft_purge(M).

is_loaded(M) when is_atom(M) ->
    case '$rt':code_loaded(M) of
        true -> {file, atom_to_list(M) ++ ".masm"};
        false -> false
    end.
