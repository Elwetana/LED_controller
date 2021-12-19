#ifndef __IP_SOURCE_H__
#define __IP_SOURCE_H__

typedef struct IPSource
{
	BasicSource basic_source;
	int first_update;
} IPSource;

extern IPSource ip_source;

#endif /* __IP_SOURCE_H__ */
