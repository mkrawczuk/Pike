#!/usr/local/bin/pike

/*
 * This script is used to process *.cmod files into *.c files, it 
 * reads Pike style prototypes and converts them into C code.
 *
 * The input can look something like this:
 *
 * PIKECLASS fnord 
 *  attributes;
 * {
 *
 *   PIKEFUNC int function_name (int x)
 *    attribute;
 *    attribute value;
 *   {
 *     C code using 'x'.
 *
 *     RETURN x;
 *   }
 * }
 *
 * All the begin_class/ADD_EFUN/ADD_FUNCTION calls will be inserted
 * instead of the word INIT in your code.
 *
 * Currently, the following attributes are understood:
 *   efun;     makes this function a global constant (no value)
 *   flags;    ID_STATIC | ID_NOMASK etc.
 *   optflags; OPT_TRY_OPTIMIZE | OPT_SIDE_EFFECT etc.
 *   type;     tInt, tMix etc. use this type instead of automatically
 *             generating type from the prototype
 *   errname;  The name used when throwing errors.
 *   name;     The name used when doing add_function.
 *
 *
 * BUGS/LIMITATIONS
 *  o Parenthesis must match, even within #if 0
 *  o Not all Pike types are supported yet.
 *  o No support for functions that takes variable number of arguments yet.
 *  o No support for class variables yet.
 *  o No support for set_init/exit/gc_mark/gc_check_callback.
 *  o No support for 'char *', 'double'  etc.
 *  o RETURN; (void) doesn't work yet
 *  o need a RETURN_NULL; or something.. RETURN 0; might work but may
 *    be confusing as RETURN x; will not work if x is zero.
 *    
 */

#define PC Parser.C

#ifdef OLD
import ".";
#define PC .C
#endif

int parse_type(array x, int pos)
{
  while(1)
  {
    while(x[pos]=="!") pos++;
    pos++;
    if(arrayp(x[pos])) pos++;
    switch(x[pos])
    {
      default:
	return pos;

      case "&":
      case "|":
	pos++;
    }
  }
}

string merge(array x)
{
  return PC.simple_reconstitute(x);
}

string cname(mixed type)
{
  mixed btype;
  if(arrayp(type))
    btype=type[0];
  else
    btype=type;

  switch(objectp(btype) ? btype->text : btype)
  {
    case "int": return "INT32";
    case "float": return "FLOAT_NUMBER";
    case "string": return "struct pike_string *";

    case "array":
    case "multiset":
    case "mapping":

    case "object":
    case "program": return "struct "+btype+" *";

    case "function":

    case "0": case "1": case "2": case "3": case "4":
    case "5": case "6": case "7": case "8": case "9":
    case "mixed":  return "struct svalue *";

    default:
      werror("Unknown type %s\n",objectp(btype) ? btype->text : btype);
      exit(1);
  }
}

string uname(mixed type)
{
  switch(objectp(type) ? type->text : type)
  {
    case "int": return "integer";
    case "float":
    case "string":

    case "array":
    case "multiset":
    case "mapping":

    case "object":
    case "program": return "struct "+type+" *";

    case "function":
    case "0": case "1": case "2": case "3": case "4":
    case "5": case "6": case "7": case "8": case "9":
    case "mixed":  return "struct svalue *";

    default:
      werror("Unknown type %s\n",type);
      exit(1);
  }
}

mapping(string:string) parse_arg(array x)
{
  mapping ret=(["name":x[-1]]);
  ret->type=x[..sizeof(x)-2];
  ret->basetype=x[0];
  if(sizeof(ret->type/({"|"}))>1)
    ret->basetype="mixed";
  ret->ctype=cname(ret->basetype);
  ret->typename=merge(recursive(strip_type_assignments,ret->type));
  ret->line=ret->name->line;
  return ret;
}

/* FIXME: this is not finished yet */
string convert_type(array s)
{
//  werror("%O\n",s);
  switch(objectp(s[0])?s[0]->text:s[0])
  {
    case "0": case "1": case "2": case "3": case "4":
    case "5": case "6": case "7": case "8": case "9":
      if(sizeof(s)>1 && s[1]=="=")
      {
	return sprintf("tSetvar(%s,%s)",s[0],convert_type(s[2..]));
      }else{
	return sprintf("tVar(%s)",s[0]);
      }

    case "int": return "tInt";
    case "float": return "tFlt";
    case "string": return "tStr";
    case "array": 
      if(sizeof(s)<2) return "tArray";
      return "tArr("+convert_type(s[1][1..sizeof(s[1])-2])+")";
    case "multiset": 
      if(sizeof(s)<2) return "tMultiset";
      return "tSet("+convert_type(s[1][1..sizeof(s[1])-2])+")";
    case "mapping": 
    {
      if(sizeof(s)<2) return "tMapping";
      mixed tmp=s[1][1..sizeof(s[1])-2];
      return "tMap("+
	convert_type((tmp/({":"}))[0])+","+
	  convert_type((tmp/({":"}))[1])+")";
    }
    case "object": 
      return "tObj";

    case "program": 
      return "tProgram";
    case "function": 
      return "tFunc";
    case "mixed": 
      return "tMix";

    default:
      return sprintf("ERR%O",s);
  }
}


string make_pop(mixed howmany)
{
  switch(howmany)
  {
    default:
      return "pop_n_elems("+howmany+");";
      
    case "0": case 0: return "";
    case "1": case 1: return "pop_stack();";
  }
}

/* Fixme:
 * This routine inserts non-tokenized strings into the data, which
 * can confuse a later stage, we might need to do something about that.
 * However, I need a *simple* way of doing it first...
 * -Hubbe
 */
array fix_return(array body, string rettype, string ctype, mixed args)
{
  int pos=0;
  
  while( (pos=search(body,PC.Token("RETURN",0),pos)) != -1)
  {
    int pos2=search(body,PC.Token(";",0),pos+1);
    body[pos]=sprintf("do { %s ret_=(",ctype);
    body[pos2]=sprintf("); %s push_%s(ret_); return; }while(0);",make_pop(args),rettype);
    pos=pos2+1;
  }

  pos=0;
  while( (pos=search(body,PC.Token("REF_RETURN",0),pos)) != -1)
  {
    int pos2=search(body,PC.Token(";",0),pos+1);
    body[pos]=sprintf("do { %s ret_=(",ctype);
    body[pos2]=sprintf("); add_ref(ret_); %s push_%s(ret_); return; }while(0);",make_pop(args),rettype);
    pos=pos2+1;
  }
  return body;
}

array strip_type_assignments(array data)
{
  int pos;

  while( (pos=search(data,PC.Token("=",0))) != -1)
    data=data[..pos-2]+data[pos+1..];
  return data;
}

array recursive(mixed func, array data, mixed ... args)
{
  array ret=({});

  foreach(data, mixed foo)
    {
      if(arrayp(foo))
      {
	ret+=({ recursive(func, foo, @args) });
      }else{
	ret+=({ foo });
      }
    }

  return func(ret, @args);
}

mapping parse_attributes(array attr)
{
  mapping attributes=([]);
  foreach(attr/ ({";"}), attr)
    {
      switch(sizeof(attr))
      {
	case 0: break;
	case 1:
	  attributes[attr[0]->text]=1;
	  break;
	default:
	  array tmp=attr[1..];
	  if(sizeof(tmp) == 1 && arrayp(tmp[0]) && tmp[0][0]=="(")
	    tmp=tmp[0][1..sizeof(tmp[0])-2];
	  
	  attributes[attr[0]->text]=merge(tmp);
      }
    }
  return attributes;
}

string file;

array convert(array x, string base)
{
  array addfuncs=({});
  array exitfuncs=({});

  array x=x/({"PIKECLASS"});
  array ret=x[0];

  for(int f=1;f<sizeof(x);f++)
  {
    array func=x[f];
    int p;
    for(p=0;p<sizeof(func);p++)
      if(arrayp(func[p]) && func[p][0]=="{")
	break;
    array proto=func[..p-1];
    array body=func[p];
    string name=proto[p]->text;
    mapping attributes=parse_attributes(proto[p+2..]);

    [ array classcode, array classaddfuncs, array classexitfuncs ]=
      convert(body[1..sizeof(body)-2],name+"_");
    ret+=({ sprintf("#define class_%s_defined\n",name), });
    ret+=classcode;

    addfuncs+=({
      sprintf("#ifdef class_%s_defined\n",name),
      "  start_new_program();\n",
    })+
      classaddfuncs+
	({
	  sprintf("  end_class(%O,%d);\n",name, attributes->flags || "0"),
	  sprintf("#iendif\n"),
	});

    exitfuncs+=({
      sprintf("#ifdef class_%s_defined\n",name),
    })+
      classexitfuncs+
	({
	  sprintf("#iendif\n"),
	});
  }


#if 0
  array thestruct=({});
  x=ret/({"PIKEVAR"});
  ret=x[0];
  for(int f=1;f<sizeof(x);f++)
  {
    array var=x[f];
    int pos=search(var,PC.Token(";",0),);
    mixed name=var[pos-1];
    array type=var[..pos-2];
    array rest=var[pos+1..];
    
  }
#endif


  x=ret/({"PIKEFUN"});
  ret=x[0];
  for(int f=1;f<sizeof(x);f++)
  {
    array func=x[f];
    int p;
    for(p=0;p<sizeof(func);p++)
      if(arrayp(func[p]) && func[p][0]=="{")
	break;

    array proto=func[..p-1];
    array body=func[p];
    array rest=func[p+1..];

    p=parse_type(proto,0);
    array rettype=proto[..p-1];
    string name=proto[p]->text;
    array args=proto[p+1];

    mapping attributes=parse_attributes(proto[p+2..]);

    args=args[1..sizeof(args)-2]/({","});

//    werror("FIX RETURN: %O\n",body);
    body=recursive(fix_return,body, rettype[0], cname(rettype), sizeof(args)); 
    
//    werror("name=%s\n",name);
//    werror("  rettype=%O\n",rettype);
//    werror("  args=%O\n",args);

    ret+=({
      sprintf("#define f_%s_defined\n",name),
      sprintf("void f_%s(INT32 args) {\n",name),
    });

    args=map(args,parse_arg);

    foreach(args, mapping arg)
      ret+=({
	PC.Token(sprintf("%s %s;\n",arg->ctype, arg->name),arg->line),
      });


    addfuncs+=({
      sprintf("#ifdef f_%s_defined\n",name),
      PC.Token(sprintf("  %s(%O,f_%s,tFunc(%s,%s),%s);\n",
		       attributes->efun ? "ADD_EFUN" : "ADD_FUNCTION",
		       attributes->name || name,
		       name,
		       attributes->type ? attributes->type :
		       Array.map(args->type,convert_type)*" ",
		       convert_type(rettype),
		       (attributes->efun ? attributes->optflags : 
			attributes->flags )|| "0" ,
		       ),proto[0]->line),
      sprintf("#endif\n",name),
    });

    int argnum;

    argnum=0;
    ret+=({
      PC.Token(sprintf("if(args != %d) wrong_number_of_args_error(%O,args,%d);\n",
		       sizeof(args),
		       name,
		       sizeof(args)), proto[0]->line)
    });

    int sp=-sizeof(args);
    foreach(args, mapping arg)
      {
	switch((string)arg->basetype)
	{
	  default:
	    ret+=({
	      PC.Token(sprintf("if(sp[%d].type != PIKE_T_%s)",
			       sp,upper_case(arg->basetype->text)),arg->line)
	    });
	    break;

	  case "program":
	    ret+=({
	      PC.Token(sprintf("if(!( %s=program_from_svalue(sp%+d)))",
			       arg->name,sp),arg->line)
	    });
	    break;

	  case "mixed":
	}

	switch((string)arg->basetype)
	{
	  default:
	    ret+=({
	      PC.Token(sprintf(" SIMPLE_BAD_ARG_ERROR(%O,%d,%O);\n",
			       attributes->errname || attributes->name || name,
			       argnum+1,
			       arg->typename),arg->line),
	    });

	  case "mixed":
	}

	switch(objectp(arg->basetype) ? arg->basetype->text : arg->basetype )
	{
	  case "int":
	    ret+=({
	      sprintf("%s=sp[%d].u.integer;\n",arg->name,sp)
	    });
	    break;

	  case "float":
	    ret+=({
	      sprintf("%s=sp[%d].u.float_number;\n",
		      arg->name,
		      sp)
	    });
	    break;


	  case "mixed":
	    ret+=({
	      PC.Token(sprintf("%s=sp%+d; dmalloc_touch_svalue(sp%+d);\n",
			       arg->name,
			       sp,sp),arg->line),
	    });
	    break;

	  default:
	    ret+=({
	      PC.Token(sprintf("debug_malloc_pass(%s=sp[%d].u.%s);\n",
			       arg->name,
			       sp,
			       arg->basetype),arg->line)
	    });

	  case "program":
	}
        argnum++;
	sp++;
      }
    
    if(sizeof(body))
      ret+=({body});
    ret+=({ "}\n" });

    ret+=rest;
  }


  return ({ ret, addfuncs, exitfuncs });
}

int main(int argc, array(string) argv)
{
  mixed x;
  file=argv[1];
  x=Stdio.read_file(file);
  x=PC.split(x);
  x=PC.tokenize(x,file);
  x=PC.hide_whitespaces(x);
  x=PC.group(x);

  array tmp=convert(x,"");
  x=tmp[0];
  x=recursive(replace,x,PC.Token("INIT",0),tmp[1]);
  x=recursive(replace,x,PC.Token("EXIT",0),tmp[2]);

  if(equal(x,tmp[0]))
  {
    // No INIT / EXIT, add our own stuff..

    x+=({
      "void pike_module_init(void) {\n",
      tmp[1],
      "}\n",
      "void pike_module_exit(void) {\n",
      tmp[2],
      "}\n",
    });
  }
  write(PC.reconstitute_with_line_numbers(x));
}
