/* $Id: mkwmml.pike,v 1.17 1999/07/02 19:23:48 mirar Exp $ */

import Stdio;
import Array;

mapping parse=([]);
int illustration_counter;

mapping manpage_suffix=
([
   "Image":"i",
   "Image.image":"i",
]);


function verbose=werror;

#define error(X) throw( ({ (X), backtrace()[0..sizeof(backtrace())-2] }) )

/*

module : mapping <- moduleM
	"desc" : text
	"see also" : array of references 
	"note" : mapping of "desc": text
	"modules" : same as classes (below)
	"classes" : mapping 
		class : mapping <- classM
	        	"see also" : array of references 
			"desc" : text
			"note" : mapping of "desc": text
			"methods" : array of mappings <- methodM
				"decl" : array of textlines of declarations
				"desc" : text
				"returns" : textline
				"see also" : array of references 
				"note" : mapping of "desc": text
				"known bugs" : mapping of "desc": text
				"args" : array of mappings <- argM
					"args" : array of args names and types
					"desc" : description
				"names" : multiset of method name(s) 

Quoting: Only '<' must be quoted as '&lt;'.

*/

mapping moduleM, classM, methodM, argM, nowM, descM;

mapping focM(mapping dest,string name,string line)
{
   if (!dest->_order) dest->_order=({});
   if (-1==search(dest->_order,name)) dest->_order+=({name});
   return dest[name] || (dest[name]=(["_line":line]));
}

string stripws(string s)
{
   sscanf(s,"%*[ \t\n\r]%s",s);
   s=reverse(s);
   sscanf(s,"%*[ \t\n\r]%s",s);
   return reverse(s);
}

mapping lower_nowM()
{
   if (nowM && 
       (nowM==parse
	|| nowM==classM
	|| nowM==methodM
	|| nowM==moduleM)) return nowM;
   else return nowM=methodM;
}

void report(string s)
{
   verbose("mkwmml:   "+s+"\n");
}

#define complain(X) (X)

mapping keywords=
(["module":lambda(string arg,string line) 
	  { classM=descM=nowM=moduleM=focM(parse,stripws(arg),line); 
	    methodM=0; 
	    if (!nowM->classes) nowM->classes=(["_order":({})]); 
	    if (!nowM->modules) nowM->modules=(["_order":({})]); 
	    report("module "+arg); },
  "class":lambda(string arg,string line) 
	  { if (!moduleM) return complain("class w/o module");
	    descM=nowM=classM=focM(moduleM->classes,stripws(arg),line); 
	    methodM=0; report("class "+arg); },
  "submodule":lambda(string arg,string line) 
	  { if (!moduleM) return complain("submodule w/o module");
	    classM=descM=nowM=moduleM=focM(moduleM->modules,stripws(arg),line);
	    methodM=0;
	    if (!nowM->classes) nowM->classes=(["_order":({})]); 
	    if (!nowM->modules) nowM->modules=(["_order":({})]); 
	    report("submodule "+arg); },
  "method":lambda(string arg,string line)
	  { if (!classM) return complain("method w/o class");
	    if (!nowM || methodM!=nowM || methodM->desc || methodM->args || descM==methodM) 
	    { if (!classM->methods) classM->methods=({});
	      classM->methods+=({methodM=nowM=(["decl":({}),"_line":line])}); }
	    methodM->decl+=({stripws(arg)}); descM=0; },
  "arg":lambda(string arg,string line)
	  {
	     if (!methodM) return complain("arg w/o method");
	     if (!methodM->args) methodM->args=({});
	       methodM->args+=({argM=nowM=(["args":({}),"_line":line])}); 
	     argM->args+=({arg}); descM=argM;
	  },
  "note":lambda(string arg,string line)
	  {
	     if (!lower_nowM()) 
	        return complain("note w/o method, class or module");
	     descM=nowM->note||(nowM->note=(["_line":line]));
	  },
  "added":lambda(string arg,string line)
	  {
	     if (!lower_nowM()) 
	        return complain("added in: w/o method, class or module");
	     descM=nowM->added||(nowM->added=(["_line":line]));
	  },
  "bugs":lambda(string arg,string line)
	  {
	     if (!lower_nowM()) 
	        return complain("bugs w/o method, class or module");
	     descM=nowM->bugs||(nowM->bugs=(["_line":line]));
	  },
  "see":lambda(string arg,string line)
	  {
	     if (arg[0..3]!="also")
	        return complain("see w/o 'also:'\n");
	     if (!lower_nowM()) 
	        return complain("see also w/o method, class or module");
	     sscanf(arg,"also%*[:]%s",arg);
	     nowM["see also"]=map(arg/",",stripws)-({""});
	     if (!nowM["see also"])
	        return complain("empty see also\n");
	  },
  "returns":lambda(string arg)
	  {
	     if (!methodM) 
	        return complain("returns w/o method");
	     methodM->returns=stripws(arg);
	     descM=0; nowM=0;
	  }
]);

string getridoftabs(string s)
{
   string res="";
   for (;;)
   {
      int i;
      if ((i=search(s,"\t"))==-1) return res+s;
      res+=s[0..i-1]; 
      s=s[i+1..];
      res+="         "[(strlen(res)%8)..7];
   }
}


object(File) make_file(string filename)
{
   stderr->write("creating "+filename+"...\n");
   if (file_size(filename)>0)
   {
      rm(filename+"~");
      mv(filename,filename+"~");
   }
   object f=File();
   if (!f->open(filename,"wtc"))
   {
      stderr->write("failed.");
      exit(1);
   }
   return f;
}

string synopsis_to_html(string s,mapping huh)
{
   string type,name,arg;
   if (sscanf(s,"%s%*[ \t]%s(%s",type,name,arg)!=4)
   {
      sscanf(s,"%s(%s",name,arg),type="";
      werror(sprintf(huh->_line+": suspicios method %O\n",(s/"(")[0]));
   }
   if (arg[..1]==")(") name+="()",arg=arg[2..];

   if (!arg) arg="";

   return 
      type+" <b>"+name+"</b>("+
      replace(arg,({","," "}),({", ","\240"}));
}

string htmlify(string s) 
{
#define HTMLIFY(S) \
   (replace((S),({"&lt;",">","&","\240"}),({"&lt;","&gt;","&amp;","&nbsp;"})))

   string t="",u,v;
   while (sscanf(s,"%s<%s>%s",u,v,s)==3)
      t+=HTMLIFY(u)+"<"+v+">";
   return t+HTMLIFY(s);
}

#define linkify(S) \
   ("\""+replace((S),({"->","()","&lt;","&gt;"}),({".","","<",">"}))+"\"")

string make_nice_reference(string what,string prefix)
{
   string q;
   if (what==prefix[strlen(prefix)-strlen(what)-2..strlen(prefix)-3])
   {
      q=prefix[0..strlen(prefix)-3];
   }
   else if (what==prefix[strlen(prefix)-strlen(what)-1..strlen(prefix)-2])
   {
      q=prefix[0..strlen(prefix)-2];
   }
   else if (search(what,".")==-1 &&
	    search(what,"->")==-1 &&
	    !parse[what])
   {
      q=prefix+what;
   }
   else 
      q=what;

   return "<link to="+linkify(q)+">"+htmlify(what)+"</link>";
}

string fixdesc(string s,string prefix,string where)
{
   s=stripws(s);

   string t,u,v,q;

   t=s; s="";
   while (sscanf(t,"%s<ref>%s</ref>%s",t,u,v)==3)
   {
      s+=htmlify(t)+make_nice_reference(u,prefix);
      t=v;
   }
   s+=htmlify(t);

   t=s; s="";
   while (sscanf(t,"%s<illustration%s>%s</illustration>%s",t,q,u,v)==4)
   {
      s+=replace(t,"\n\n","\n\n<p>");

      s+="<illustration __from__='"+where+"' src=image_ill.pnm"+q+">\n"
	 +replace(u,"lena()","src")+"</illustration>";

      t=v;
   }
   s+=replace(t,"\n\n","\n\n<p>");

   return s;
}


multiset(string) get_method_names(string *decls)
{
   string decl,name;
   multiset(string) names=(<>);
   foreach (decls,decl)
   {
      sscanf(decl,"%*s%*[\t ]%s%*[\t (]%*s",name);
      names[name]=1;
   }
   return names;
}

string *nice_order(string *arr)
{
   sort(map(arr,replace,({"_","`"}),({"�","�"})),
	arr);
   return arr;
}

string addprefix(string suffix,string prefix)
{
   return prefix+suffix;
}

array fix_dotstuff(array(string) in)
{
   if (!sizeof(in)) return ({});
   array(string) last;
   in=Array.map(in,replace,({"->",">","<"}),({".","&lt;","&gt;"}));
   last=in[0]/"."; 
   last=last[..sizeof(last)-2];
   int i;
   array res=in[..0];
   for (i=1; i<sizeof(in); i++)
   {
      array(string) z=in[i]/".";
      if (equal(z[..sizeof(z)-2],last))
	 res+=({"."+z[-1]});
      else
      {
	 last=z[..sizeof(z)-2];
	 res+=in[i..i];
      }
   }
   return res;
}

void document(string enttype,
	      mapping huh,string name,string prefix,
	      object f)
{
   string *names;

   if (huh->names)
      names=map(indices(huh->names),addprefix,name);
   else
      names=({name});

   verbose("mkwmml: "+name+" : "+names*","+"\n");

   f->write("\n<!-- " + huh->_line + " -->\n");
   f->write("<"+enttype+" name="+
	    fix_dotstuff(names)*",");

   if (manpage_suffix[replace(name,"->",".")])
      f->write(" mansuffix="+manpage_suffix[replace(name,"->",".")]);

   f->write(">\n");

// [SYNTAX]

   if (huh->decl)
   {
      f->write("<man_syntax>\n");

      f->write(replace(htmlify(map(huh->decl,synopsis_to_html,huh)*
			       "<br>\n"),"\n","\n\t")+"\n");

      f->write("</man_syntax>\n\n");
   }

// [DESCRIPTION]

   if (huh->desc)
   {
      f->write("<man_description>\n");
      f->write(fixdesc(huh->desc,prefix,huh->_line)+"\n");
      f->write("</man_description>\n\n");
   }

// [ARGUMENTS]

   if (huh->args)
   {
      string rarg="";
      f->write("<man_arguments>\n");
      mapping arg;
      foreach (huh->args, arg)
      {
	 if (arg->desc)
	 {
	    f->write("\t<aargdesc>\n"
		     +fixdesc(rarg+"\t\t<aarg>"
			      +arg->args*"</aarg>\n\t\t<aarg>"
			      +"</aarg>",prefix,arg->_line)
		     +"\n\t\t<adesc>"
		     +fixdesc(arg->desc,prefix,arg->_line)
		     +"</adesc></aargdesc>\n\n");
	    rarg="";
	 }
	 else
	 {
	    rarg+="\t\t<aarg>"
	       +arg->args*"</aarg>\n\t\t<aarg>"+
	       "</aarg>\n";
	 }
      }
      if (rarg!="") error("trailing args w/o desc on "+arg->_line+"\n");

      f->write("</man_arguments>\n\n");
   }

// [RETURN VALUE]

   if (huh->returns)
   {
      f->write("<man_returns>\n");
      f->write(fixdesc(huh->returns,prefix,huh->_line)+"\n");
      f->write("</man_returns>\n\n");
   }


// [NOTE]

   if (huh->note && huh->note->desc)
   {
      f->write("<man_note>\n");
      f->write(fixdesc(huh->note->desc,prefix,huh->_line)+"\n");
      f->write("</man_note>\n\n");
   }

// [BUGS]

   if (huh->bugs && huh->bugs->desc)
   {
      f->write("<man_bugs>\n");
      f->write(fixdesc(huh->bugs->desc,prefix,huh->_line)+"\n");
      f->write("</man_bugs>\n\n");
   }

// [ADDED]

   if (huh->added && huh->added->desc)
   {
      /* noop */
   }

// [SEE ALSO]

   if (huh["see also"])
   {
      f->write("<man_see exp>\n");
      f->write(htmlify(huh["see also"]*", "));
      f->write("</man_see>\n\n");
   }

// ---childs----

   if (huh->methods)
   {
      // postprocess methods to get names

      multiset(string) method_names=(<>);
      string *method_names_arr,method_name;
      mapping method;

      if (huh->methods) 
	 foreach (huh->methods,method)
	    method_names|=(method->names=get_method_names(method->decl));

       method_names_arr=nice_order(indices(method_names));

      // alphabetically

      foreach (method_names_arr,method_name)
	 if (method_names[method_name])
	 {
	    // find it
	    foreach (huh->methods,method)
	       if ( method->names[method_name] )
	       {
		  document("method",method,prefix,prefix,f);
		  method_names-=method->names;
	       }
	    if (method_names[method_name])
	       stderr->write("failed to find "+method_name+" again, wierd...\n");
	 }
   }

   if (huh->classes)
   {
      foreach(huh->classes->_order,string n)
      {
	 f->write("\n\n\n<section title=\""+prefix+n+"\">\n");
	 document("class",huh->classes[n],
		  prefix+n,prefix+n+"->",f);
	 f->write("</section title=\""+prefix+n+"\">\n");
      }
   }

   if (huh->modules)
   {
      foreach(huh->modules->_order,string n)
      {
	 f->write("\n\n\n<section title=\""+prefix+n+"\">\n");
	 document("module",huh->modules[n],
		  prefix+n,prefix+n+".",f);
	 f->write("</section title=\""+prefix+n+"\">\n");
      }
   }
// end ANCHOR

   f->write("</"+enttype+" name="+
	    fix_dotstuff(names)*","+">");
}

void make_doc_files()
{
   stderr->write("modules: "+sort(indices(parse))*", "+"\n");
   
   foreach (sort(indices(parse)-({"_order"})),string module)
      document("module",parse[module],module,module+".",stdout);
}

int main(int ac,string *files)
{
   string s,t;
   int line;
   string *ss=({""});
   object f;

   string currentfile;

   nowM=parse;

   stderr->write("reading and parsing data...\n");

   files=files[1..];

   if (sizeof(files) && files[0]=="--nonverbose") 
      files=files[1..],verbose=lambda(){};

   stderr->write("mkwmml: reading files...\n");

   for (;;)
   {
      int i;

      if (!f) 
      {
	 if (!sizeof(files)) break;
	 verbose("mkwmml: reading "+files[0]+"...\n");
	 f=File();
	 currentfile=files[0];
	 files=files[1..];
	 if (!f->open(currentfile,"r")) { f=0; continue; }
	 t=0;
	 ss=({""});
	 line=0;
      }

      if (sizeof(ss)<2)
      {
	 if (t=="") { f=0; continue; }
	 t=f->read(8192);
	 s=ss[0];
	 ss=t/"\n";
	 ss[0]=s+ss[0];
      }
      s=ss[0]; ss=ss[1..];

      line++;
      if ((i=search(s,"**!"))!=-1 || (i=search(s,"//!"))!=-1)
      {
	 string kw,arg;

	 sscanf(s[i+3..],"%*[ \t]%[^: \t\n\r]%*[: \t]%s",kw,arg);
	 if (keywords[kw])
	 {
	    string err;
	    if ( (err=keywords[kw](arg,currentfile+" line "+line)) )
	    {
	       stderr->write("mkwmml: "+
			     currentfile+" line "+line+": "+err+"\n");
	       return 1;
	    }
	 }
	 else 
	 {
	    string d=s[i+3..];
	    sscanf(d,"%*[ \t]!%s",d);
//	    if (search(s,"$Id")!=-1) report("Id: "+d);
	    if (!descM) descM=methodM;
	    if (!descM)
	    {
	       stderr->write("mkwmml: "+
			     currentfile+" line "+line+
			     ": illegal description position\n");
	       return 1;
	    }
	    if (!descM->desc) descM->desc="";
	    else descM->desc+="\n";
	    d=getridoftabs(d);
	    descM->desc+=d;
	 }
      }
   }

//   stderr->write(sprintf("%O",parse));

   stderr->write("mkwmml: making docs...\n\n");

   make_doc_files();

   return 0;
}
