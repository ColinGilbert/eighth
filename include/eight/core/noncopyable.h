//------------------------------------------------------------------------------
#pragma once
namespace eight {
//------------------------------------------------------------------------------

class NonCopyable
{
public:
	NonCopyable(){}
private:
	NonCopyable( const NonCopyable& );
	NonCopyable& operator=( const NonCopyable& );
};

class NoCreate
{
private:
	NoCreate(){}
};

//------------------------------------------------------------------------------
} // namespace eight
//------------------------------------------------------------------------------
