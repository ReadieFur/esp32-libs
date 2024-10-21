//Based on the stream debugger by Volodymyr Shymanskyy.

#include <Stream.h>

class StreamDebugger : public Stream
{
public:
	Stream* DumpStream;

	StreamDebugger(Stream& dataStream, Stream* dumpStream) : _dataStream(dataStream), DumpStream(dumpStream) {}

	virtual size_t write(uint8_t ch)
	{
		if (DumpStream != nullptr)
			DumpStream->write(ch);
		return _dataStream.write(ch);
	}

	virtual int read()
	{
		int ch = _dataStream.read();
		if (ch != -1 && DumpStream != nullptr)
			DumpStream->write(ch);
		return ch;
	}

	virtual int available() { return _dataStream.available(); }

	virtual int peek() { return _dataStream.peek(); }

	virtual void flush() { _dataStream.flush(); }

private:
	Stream& _dataStream;
};
