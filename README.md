# DuxTape

## THIS IS NOT MEANT TO BE USED EVER PLEASE DON'T USE IT

DukTape engine embedded into the Beam.

```elixir
Interactive Elixir (1.7.3) - press Ctrl+C to exit (type h() ENTER for help)
iex(1)> {:ok, conn} = DuxTape.open([])             
{:ok,
 {:connection, #Reference<0.2819442121.3922460673.217860>,
  #Reference<0.2819442121.3922591745.217858>}}
iex(2)> DuxTape.eval(conn, 'print("this is a debug print");')   
this is a debug print

```

## Installation

If [available in Hex](https://hex.pm/docs/publish), the package can be installed
by adding `dux_tape` to your list of dependencies in `mix.exs`:

```elixir
def deps do
  [
    {:dux_tape, "~> 0.1.0"}
  ]
end
```

Documentation can be generated with [ExDoc](https://github.com/elixir-lang/ex_doc)
and published on [HexDocs](https://hexdocs.pm). Once published, the docs can
be found at [https://hexdocs.pm/dux_tape](https://hexdocs.pm/dux_tape).

