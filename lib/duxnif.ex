defmodule DuxTapeNif do
  @on_load :load_nif
  @doc false
  def load_nif do
    nif_file = Path.join(:code.priv_dir(:duxtape), "duxtape_nif") |> to_charlist()

    case :erlang.load_nif(nif_file, 0) do
      :ok -> :ok
      {:error, {:reload, _}} -> :ok
      {:error, reason} -> {:error, :load_failed, reason}
    end
  end

  def start(), do: nif_error()
  def open(_db, _ref, _dest, _arg), do: nif_error()
  def close(_db, _ref, _dest), do: nif_error()
  def eval(_db, _ref, _dest, _data), do: nif_error()

  defp nif_error, do: :erlang.nif_error(:nif_not_loaded)
end
