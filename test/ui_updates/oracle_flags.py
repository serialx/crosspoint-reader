"""Expose state only to the isolated update-screen render fixture."""
Import("env")
env.Append(CXXFLAGS=["-fno-access-control"])
