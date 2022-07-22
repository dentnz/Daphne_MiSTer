#include <ctype.h>	// for toupper

class numstr
{
public:
	static int ToInt32(const char *str);
	static unsigned int my_strlen(const char *s);
private:
	inline static bool is_digit(char ch, int base);
};

// NOTE : this function doesn't do full safety checking in the interest of simplicity and speed
int numstr::ToInt32(const char *str)
{
	const int BASE = 10;	// for now we always assume base is 10 because I never seem to use anything else
	int result = 0;
	bool found_first_digit = false;	// whether we have found the first digit or not
	int sign_mult = 1;	// 1 if the number is positive, -1 if it's negative

	for (unsigned int i = 0; i < my_strlen(str); i++)
	{
		if (!found_first_digit)
		{
			if (is_digit(str[i], BASE))
			{
				found_first_digit = true;
			}

			// if it a negative number?
			else if (str[i] == '-')
			{
				sign_mult = -1;
			}

			// else it's an unknown character, so we ignore it until we get to the first digit
		}

		// note: we do not want this to be an "else if" because the above 'if' needs to flow into this
		if (found_first_digit)
		{
			// make sure we aren't dealing with any non-integers
			if ((str[i] >= '0') && (str[i] <= '9'))
			{
				result *= BASE;
				result += str[i] - '0';
			}
			// else we've hit unknown characters so we're done
			else
			{
				break;
			}
		}
	}

	return result * sign_mult;
}

unsigned int numstr::my_strlen(const char *s)
{
	unsigned int i = 0;
	while (s[i] != 0) i++;
	return i;
}

////////////////////////////////
// private funcs

inline bool numstr::is_digit(char ch, int base)
{
	if ((base == 10) && (ch >= '0') && (ch <= '9')) return(true);
	else if ((base == 16) && (
		((ch >= '0') && (ch <= '9')) ||
		((toupper(ch) >= 'A') && (toupper(ch) <= 'F'))
		)) return(true);
	// else no other base is supported

	return false;
}