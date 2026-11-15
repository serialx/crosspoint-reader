"""Source fragments shared by tests that compile the production input adapters."""

def reader_services(source):
    start = source.index('template <bool Raw>')
    return source[start:source.index('inline XpUiReaderInputServices inputSources(', start)]


def snapshot(source):
    start = source.index('template <bool ExplicitHome')
    return source[start:]
