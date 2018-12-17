defmodule DuxTape do
  @default_timeout_ms 5000

  def open(arg, timeout \\ @default_timeout_ms) do
    {:ok, conn} = DuxTapeNif.start()
    ref = make_ref()
    :ok = DuxTapeNif.open(conn, ref, self(), arg)
    case receive_answer(ref, timeout) do
      :ok -> {:ok, {:connection, make_ref(), conn}}
      {:error, _} = error -> error
    end
  end

  def close({:connection, _ref, conn}, timeout \\ @default_timeout_ms) do
    ref = make_ref()
    :ok = DuxTapeNif.close(conn, ref, self()) 
    receive_answer(ref, timeout)
  end

  def eval({:connection, _ref, conn}, data, timeout \\ @default_timeout_ms) do
    ref = make_ref()
    :ok = DuxTapeNif.eval(conn, ref, self(), data) 
    receive_answer(ref, timeout)
  end

  defp receive_answer(ref, timeout) do
    start = :os.timestamp()
    receive do
      {:duxtape, ^ref, resp} -> resp
      {:duxtape, _ref, resp} ->
        IO.inspect(resp, label: "ignoring stale duxtape response")
        passed_mics = :timer.now_diff(:os.timestamp(), start) |> div(1000)
        new_timeout = (timeout - passed_mics)
        new_timeout = new_timeout < 0 && 0 || new_timeout
        receive_answer(ref, new_timeout)
    after
      timeout -> {:error, :timeout}
    end
  end
end
