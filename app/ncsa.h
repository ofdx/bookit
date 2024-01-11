
// Start NCSA code
static char x2c(char *what){
	char digit;
	digit=(what[0]>='A'?((what[0]&0xdf)-'A')+10:(what[0]-'0'));
	digit*=16;
	digit+=(what[1]>='A'?((what[1]&0xdf)-'A')+10:(what[1]-'0'));
	return digit;
}

void unescape_url(char *url){
	int x, y;
	for(x=0, y=0;url[y];++x, ++y){
		if((url[x]=url[y])=='%'){
			url[x]=x2c(&url[y+1]);
			y+=2;
		}
	}
	url[x]=0;
}
// End NCSA code


// Wrapper for C++ std::string
void unescape_url_param(std::string &url){
	char *curl = strdup(url.c_str());

	// Fix + splace encoding
	for(char *a = curl; *a; ++ a)
		if(*a == '+')
			*a = ' ';

	// Fix percent encoding.
	unescape_url(curl);
	url = curl;

	free(curl);
}
