/* see copyright notice in squirrel.h */
#include <squirrel.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <sqstdstring.h>


//#define  _DEBUG

#ifdef _DEBUG
#include <stdio.h>

static const SQChar *g_nnames[] =
{
    _SC("NONE"),_SC("OP_GREEDY"),   _SC("OP_OR"),
    _SC("OP_EXPR"),_SC("OP_NOCAPEXPR"),_SC("OP_DOT"),   _SC("OP_CLASS"),
    _SC("OP_CCLASS"),_SC("OP_NCLASS"),_SC("OP_RANGE"),_SC("OP_CHAR"),
    _SC("OP_EOL"),_SC("OP_BOL"),_SC("OP_WB"),_SC("OP_MB"),_SC("OP_LAZY")
};

#endif



#define OP_GREEDY       (MAX_CHAR+1) // * + ? {n}
#define OP_OR           (MAX_CHAR+2)
#define OP_EXPR         (MAX_CHAR+3) //parentesis ()
#define OP_NOCAPEXPR    (MAX_CHAR+4) //parentesis (?:)
#define OP_DOT          (MAX_CHAR+5)
#define OP_CLASS        (MAX_CHAR+6)
#define OP_CCLASS       (MAX_CHAR+7)
#define OP_NCLASS       (MAX_CHAR+8) //negates class the [^
#define OP_RANGE        (MAX_CHAR+9)
#define OP_CHAR         (MAX_CHAR+10)
#define OP_EOL          (MAX_CHAR+11)
#define OP_BOL          (MAX_CHAR+12)
#define OP_WB           (MAX_CHAR+13)
#define OP_MB           (MAX_CHAR+14) //match balanced
#define OP_LAZY         (MAX_CHAR+15) //*? +? ?? {}?

#define SQREX_SYMBOL_ANY_CHAR ('.')
#define SQREX_SYMBOL_GREEDY_ONE_OR_MORE ('+')
#define SQREX_SYMBOL_GREEDY_ZERO_OR_MORE ('*')
#define SQREX_SYMBOL_GREEDY_ZERO_OR_ONE ('?')
#define SQREX_SYMBOL_BRANCH ('|')
#define SQREX_SYMBOL_END_OF_STRING ('$')
#define SQREX_SYMBOL_BEGINNING_OF_STRING ('^')
#define SQREX_SYMBOL_ESCAPE_CHAR ('\\')


 
  
typedef int SQRexNodeType;

typedef struct tagSQRexNode{
  SQRexNodeType type;
  SQInteger left;
  SQInteger right;
  SQInteger next;
  SQInteger parent;
  SQBool used;
}SQRexNode;

struct SQRex{
    const SQChar *_eol;
    const SQChar *_bol;
    const SQChar *_p;
    SQInteger _first;
    SQInteger _op;
    SQRexNode *_nodes;
    SQInteger _nallocated;
    SQInteger _nsize;
    SQInteger _nsubexpr;
    SQRexMatch *_matches;
    SQInteger _currsubexp;
    void *_jmpbuf;
    const SQChar **_error;
};




static SQInteger sqstd_rex_list(SQRex *exp);

static SQInteger sqstd_rex_newnode(SQRex *exp, SQRexNodeType type)
{

  /*if(type>=MAX_CHAR)
    {
      printf("1 new node type is %s\n",g_nnames[type-MAX_CHAR]);
    }
 else
   {
     printf("2 new node type is %c\n",type);
   }
  */
    SQRexNode n;
    n.type = type;
    n.next = n.right = n.left = n.parent = -1;
    n.used = false;
    if(type == OP_EXPR)
        n.right = exp->_nsubexpr++;
    if(exp->_nallocated < (exp->_nsize + 1)) {
        SQInteger oldsize = exp->_nallocated;
        exp->_nallocated *= 2;
        exp->_nodes = (SQRexNode *)sq_realloc(exp->_nodes, oldsize * sizeof(SQRexNode) ,exp->_nallocated * sizeof(SQRexNode));
    }
    exp->_nodes[exp->_nsize++] = n;
    SQInteger newid = exp->_nsize - 1;
    return (SQInteger)newid;
}

static void sqstd_rex_error(SQRex *exp,const SQChar *error)
{
    if(exp->_error) *exp->_error = error;
    longjmp(*((jmp_buf*)exp->_jmpbuf),-1);
}

static void sqstd_rex_expect(SQRex *exp, SQInteger n){
    if((*exp->_p) != n)
        sqstd_rex_error(exp, _SC("expected paren"));
    exp->_p++;
}

static SQChar sqstd_rex_escapechar(SQRex *exp)
{
    if(*exp->_p == SQREX_SYMBOL_ESCAPE_CHAR){
        exp->_p++;
        switch(*exp->_p) {
        case 'v': exp->_p++; return '\v';
        case 'n': exp->_p++; return '\n';
        case 't': exp->_p++; return '\t';
        case 'r': exp->_p++; return '\r';
        case 'f': exp->_p++; return '\f';
        default: return (*exp->_p++);
        }
    } else if(!scisprint(*exp->_p)) sqstd_rex_error(exp,_SC("letter expected"));
    return (*exp->_p++);
}

static SQInteger sqstd_rex_charclass(SQRex *exp,SQInteger classid)
{
    SQInteger n = sqstd_rex_newnode(exp,OP_CCLASS);
    exp->_nodes[n].left = classid;
    return n;
}

static SQInteger sqstd_rex_charnode(SQRex *exp,SQBool isclass)
{
    SQChar t;
    if(*exp->_p == SQREX_SYMBOL_ESCAPE_CHAR) {
        exp->_p++;
        switch(*exp->_p) {
            case 'n': exp->_p++; return sqstd_rex_newnode(exp,'\n');
            case 't': exp->_p++; return sqstd_rex_newnode(exp,'\t');
            case 'r': exp->_p++; return sqstd_rex_newnode(exp,'\r');
            case 'f': exp->_p++; return sqstd_rex_newnode(exp,'\f');
            case 'v': exp->_p++; return sqstd_rex_newnode(exp,'\v');
            case 'a': case 'A': case 'w': case 'W': case 's': case 'S':
            case 'd': case 'D': case 'x': case 'X': case 'c': case 'C':
            case 'p': case 'P': case 'l': case 'u':
                {
                t = *exp->_p; exp->_p++;
                return sqstd_rex_charclass(exp,t);
                }
            case 'm':
                {
                     SQChar cb, ce; //cb = character begin match ce = character end match
                     cb = *++exp->_p; //skip 'm'
                     ce = *++exp->_p;
                     exp->_p++; //points to the next char to be parsed
                     if ((!cb) || (!ce)) sqstd_rex_error(exp,_SC("balanced chars expected"));
                     if ( cb == ce ) sqstd_rex_error(exp,_SC("open/close char can't be the same"));
                     SQInteger node =  sqstd_rex_newnode(exp,OP_MB);
                     exp->_nodes[node].left = cb;
                     exp->_nodes[node].right = ce;
                     return node;
                }
            case 'b':
            case 'B':
                if(!isclass) {
                    SQInteger node = sqstd_rex_newnode(exp,OP_WB);
                    exp->_nodes[node].left = *exp->_p;
                    exp->_p++;
                    return node;
                } //else default
            default:
                t = *exp->_p; exp->_p++;
                return sqstd_rex_newnode(exp,t);
        }
    }
    else if(!scisprint(*exp->_p)) {

        sqstd_rex_error(exp,_SC("letter expected"));
    }
    t = *exp->_p; exp->_p++;
    return sqstd_rex_newnode(exp,t);
}
static SQInteger sqstd_rex_class(SQRex *exp)
{
    SQInteger ret = -1;
    SQInteger first = -1,chain;
    if(*exp->_p == SQREX_SYMBOL_BEGINNING_OF_STRING){
        ret = sqstd_rex_newnode(exp,OP_NCLASS);
        exp->_p++;
    }else ret = sqstd_rex_newnode(exp,OP_CLASS);

    if(*exp->_p == ']') sqstd_rex_error(exp,_SC("empty class"));
    chain = ret;
    while(*exp->_p != ']' && exp->_p != exp->_eol) {
        if(*exp->_p == '-' && first != -1){
            SQInteger r;
            if(*exp->_p++ == ']') sqstd_rex_error(exp,_SC("unfinished range"));
            r = sqstd_rex_newnode(exp,OP_RANGE);
            if(exp->_nodes[first].type>*exp->_p) sqstd_rex_error(exp,_SC("invalid range"));
            if(exp->_nodes[first].type == OP_CCLASS) sqstd_rex_error(exp,_SC("cannot use character classes in ranges"));
            exp->_nodes[r].left = exp->_nodes[first].type;
            SQInteger t = sqstd_rex_escapechar(exp);
            exp->_nodes[r].right = t;
            exp->_nodes[chain].next = r;
	    exp->_nodes[r].parent = chain;
            chain = r;
            first = -1;
        }
        else{
            if(first!=-1){
                SQInteger c = first;
                exp->_nodes[chain].next = c;
		exp->_nodes[c].parent = chain;
                chain = c;
                first = sqstd_rex_charnode(exp,SQTrue);
            }
            else{
                first = sqstd_rex_charnode(exp,SQTrue);
            }
        }
    }
    if(first!=-1){
        SQInteger c = first;
        exp->_nodes[chain].next = c;
	exp->_nodes[c].parent = chain;
    }
    /* hack? */
    exp->_nodes[ret].left = exp->_nodes[ret].next;
    exp->_nodes[ret].next = -1;
    return ret;
}

static SQInteger sqstd_rex_parsenumber(SQRex *exp)
{
    SQInteger ret = *exp->_p-'0';
    SQInteger positions = 10;
    exp->_p++;
    while(isdigit(*exp->_p)) {
        ret = ret*10+(*exp->_p++-'0');
        if(positions==1000000000) sqstd_rex_error(exp,_SC("overflow in numeric constant"));
        positions *= 10;
    };
    return ret;
}

static SQInteger sqstd_rex_element(SQRex *exp)
{
    SQInteger ret = -1;
    switch(*exp->_p)
    {
    case '(': {
        SQInteger expr;
        exp->_p++;


        if(*exp->_p =='?') {
            exp->_p++;
            sqstd_rex_expect(exp,':');
            expr = sqstd_rex_newnode(exp,OP_NOCAPEXPR);
        }
        else
            expr = sqstd_rex_newnode(exp,OP_EXPR);
        SQInteger newn = sqstd_rex_list(exp);
        exp->_nodes[expr].left = newn;
	exp->_nodes[newn].parent = expr;
        ret = expr;
        sqstd_rex_expect(exp,')');
              }
              break;
    case '[':
        exp->_p++;
        ret = sqstd_rex_class(exp);
        sqstd_rex_expect(exp,']');
        break;
    case SQREX_SYMBOL_END_OF_STRING: exp->_p++; ret = sqstd_rex_newnode(exp,OP_EOL);break;
    case SQREX_SYMBOL_ANY_CHAR: exp->_p++; ret = sqstd_rex_newnode(exp,OP_DOT);break;
    default:
        ret = sqstd_rex_charnode(exp,SQFalse);
        break;
    }


    SQBool isgreedy = SQFalse;
    unsigned short p0 = 0, p1 = 0;
    switch(*exp->_p){
        case SQREX_SYMBOL_GREEDY_ZERO_OR_MORE: p0 = 0; p1 = 0xFFFF; exp->_p++; isgreedy = SQTrue; break;
        case SQREX_SYMBOL_GREEDY_ONE_OR_MORE: p0 = 1; p1 = 0xFFFF; exp->_p++; isgreedy = SQTrue; break;
        case SQREX_SYMBOL_GREEDY_ZERO_OR_ONE: p0 = 0; p1 = 1; exp->_p++; isgreedy = SQTrue; break;
        case '{':
	  exp->_p++;
	  if(!isdigit(*exp->_p)) sqstd_rex_error(exp,_SC("number expected"));
	  p0 = (unsigned short)sqstd_rex_parsenumber(exp);
	  /*******************************/
	  switch(*exp->_p) {
	  case '}':
            p1 = p0; exp->_p++;
            break;
	  case ',':
            exp->_p++;
            p1 = 0xFFFF;
            if(isdigit(*exp->_p)){
	      p1 = (unsigned short)sqstd_rex_parsenumber(exp);
            }
            sqstd_rex_expect(exp,'}');
            break;
	  default:
            sqstd_rex_error(exp,_SC(", or } expected"));
	  }
	  /*******************************/
	  isgreedy = SQTrue;
	  break;

    }
    if(isgreedy) {
        SQInteger nnode = sqstd_rex_newnode(exp,OP_GREEDY);
        exp->_nodes[nnode].left = ret;
	exp->_nodes[ret].parent = nnode;
        exp->_nodes[nnode].right = ((p0)<<16)|p1;
        ret = nnode;

	if(*exp->_p == SQREX_SYMBOL_GREEDY_ZERO_OR_ONE)
	  {
	    exp->_p++;
	    SQInteger lazy_node = sqstd_rex_newnode(exp,OP_LAZY);
	    exp->_nodes[lazy_node].left = ret;
	    exp->_nodes[ret].parent = lazy_node;
	    ret=lazy_node;
	  }	
    }

    if((*exp->_p != SQREX_SYMBOL_BRANCH) && (*exp->_p != ')') && (*exp->_p != SQREX_SYMBOL_GREEDY_ZERO_OR_MORE) && (*exp->_p != SQREX_SYMBOL_GREEDY_ONE_OR_MORE) && (*exp->_p != '\0')) {
        SQInteger nnode = sqstd_rex_element(exp);
        exp->_nodes[ret].next = nnode;
	exp->_nodes[nnode].parent = ret;
    }

    return ret;
}

static SQInteger sqstd_rex_list(SQRex *exp)
{
    SQInteger ret=-1,e;
    if(*exp->_p == SQREX_SYMBOL_BEGINNING_OF_STRING) {
        exp->_p++;
        ret = sqstd_rex_newnode(exp,OP_BOL);
    }
    e = sqstd_rex_element(exp);
    if(ret != -1) {
        exp->_nodes[ret].next = e;
	exp->_nodes[e].parent = ret;
    }
    else ret = e;

    if(*exp->_p == SQREX_SYMBOL_BRANCH) {
        SQInteger temp,tright;
        exp->_p++;
        temp = sqstd_rex_newnode(exp,OP_OR);
        exp->_nodes[temp].left = ret;
	exp->_nodes[ret].parent = temp;
        tright = sqstd_rex_list(exp);
        exp->_nodes[temp].right = tright;
	exp->_nodes[tright].parent = temp;
        ret = temp;
    }
    return ret;
}

static SQBool sqstd_rex_matchcclass(SQInteger cclass,SQChar c)
{
    switch(cclass) {
    case 'a': return isalpha(c)?SQTrue:SQFalse;
    case 'A': return !isalpha(c)?SQTrue:SQFalse;
    case 'w': return (isalnum(c) || c == '_')?SQTrue:SQFalse;
    case 'W': return (!isalnum(c) && c != '_')?SQTrue:SQFalse;
    case 's': return isspace(c)?SQTrue:SQFalse;
    case 'S': return !isspace(c)?SQTrue:SQFalse;
    case 'd': return isdigit(c)?SQTrue:SQFalse;
    case 'D': return !isdigit(c)?SQTrue:SQFalse;
    case 'x': return isxdigit(c)?SQTrue:SQFalse;
    case 'X': return !isxdigit(c)?SQTrue:SQFalse;
    case 'c': return iscntrl(c)?SQTrue:SQFalse;
    case 'C': return !iscntrl(c)?SQTrue:SQFalse;
    case 'p': return ispunct(c)?SQTrue:SQFalse;
    case 'P': return !ispunct(c)?SQTrue:SQFalse;
    case 'l': return islower(c)?SQTrue:SQFalse;
    case 'u': return isupper(c)?SQTrue:SQFalse;
    }
    return SQFalse; /*cannot happen*/
}

static SQBool sqstd_rex_matchclass(SQRex* exp,SQRexNode *node,SQChar c)
{
    do {
        switch(node->type) {
            case OP_RANGE:
                if(c >= node->left && c <= node->right) return SQTrue;
                break;
            case OP_CCLASS:
                if(sqstd_rex_matchcclass(node->left,c)) return SQTrue;
                break;
            default:
                if(c == node->type)return SQTrue;
        }
    } while((node->next != -1) && (node = &exp->_nodes[node->next]));
    return SQFalse;
}

//SQChar *sqstd_rex_matchnode(SQRex* exp,SQRexNode *node,SQChar *str,SQRexNode *next)
static const SQChar *sqstd_rex_matchnode(SQRex* exp,SQRexNode *node,const SQChar *str,SQRexNode *next,SQBool* success,const SQChar **total_str)
{
  if(*success==true)return *total_str;

  node->used=true;
  SQRexNodeType type = node->type;
  
  switch(type)
    {
    case OP_LAZY:
      {
	SQRexNode *corresponding_greedy=&exp->_nodes[node->left];
	
	SQRexNode *greedystop = NULL;
        SQInteger p0 = (corresponding_greedy->right >> 16)&0x0000FFFF, p1 = corresponding_greedy->right&0x0000FFFF, nmaches = 0;
#ifdef _DEBUG	
	//printf("p0 is %d, p1 is %d\n",p0,p1);
#endif	
	const SQChar *s=str;//, *good = str;	
	
        if(node->next != -1) {
            greedystop = &exp->_nodes[node->next];
        }
        else {
	  SQRexNode *stop_parent = &exp->_nodes[node->parent];
	  SQBool no_parent=false;
	  while(stop_parent->next==-1 || exp->_nodes[stop_parent->next].used==true)
	    {
	      if(stop_parent->parent!=-1)
		stop_parent=&exp->_nodes[stop_parent->parent];
	      else
		{		  
		  no_parent=true;
		}
	      
	    }
	  if(no_parent==false)
	    greedystop=&exp->_nodes[stop_parent->next];

	 
        }


	
	const SQChar *s_temp=s;
	const SQChar *s_temp_head=s;

	if(corresponding_greedy->left!=-1) 
	  {

	    while(s_temp != NULL && s_temp < exp->_eol)
	      {
			
		if(nmaches>=p0)
		  {
		    if(greedystop)
		      {	   			
			   
			SQRexNode *tempgreedystop=greedystop;			    
			   
			do // (s_temp_head!=NULL)
			  {
			    if(*success==true)return *total_str;
#ifdef _DEBUG			 
			    if(tempgreedystop->type>255)printf("char is %c,stop is %s\n",*s_temp_head,g_nnames[tempgreedystop->type-255]);
			    else
			      printf("char is %c,stop is %c\n",*s_temp_head,tempgreedystop->type);
#endif	

			    
			    s_temp_head = sqstd_rex_matchnode(exp,tempgreedystop,s_temp_head,next,success,total_str);
			  

			    if(s_temp_head==NULL)
			      {			      
				break;
			      }
#ifdef _DEBUG
			    printf("find!,next is %c\n",*s_temp_head);
#endif	
			    if(exp->_nodes[tempgreedystop->parent].type==258) //EXPR
			      {
				
				exp->_matches[exp->_nodes[tempgreedystop->parent].right].len = s_temp_head - exp->_matches[exp->_nodes[tempgreedystop->parent].right].begin-1;
#ifdef _DEBUG
				printf("process the expression! right=%d, len=%d\n",stop_parent->right,exp->_matches[stop_parent->right].len);
#endif				
			      }
			  
			    if(tempgreedystop->next!=-1)
			      {
				tempgreedystop=&exp->_nodes[tempgreedystop->next];				
			      
			      }
			    else //if(next!=NULL)
			      {
				SQRexNode *stop_parent = &exp->_nodes[tempgreedystop->parent];
				while(stop_parent->next==-1 || exp->_nodes[stop_parent->next].used==true)
				  {
				    if(stop_parent->parent!=-1)
				      stop_parent=&exp->_nodes[stop_parent->parent];
				    else
				      {
					*success=true;
					*total_str=s_temp_head;
					return s_temp_head;///NULL;///??????
				      }
				    
				  }
				tempgreedystop=&exp->_nodes[stop_parent->next];
				
			      }
			  }while(1);  		  
			    
			
		      }
		    else
		      {
			
			*success=true;
			*total_str=s_temp;
			return s_temp;
		      }
		  }
		s_temp_head=s_temp;
	

	
		
		s_temp=sqstd_rex_matchnode(exp,&exp->_nodes[corresponding_greedy->left],s_temp,NULL,success,total_str);	
		if(s_temp==NULL)return NULL;
		nmaches++;		
		if(nmaches>p1)break;  //exceeds the nmaches maximum extent.	
	      }		   	    
	  }     	
	return NULL;	
      }
      
    case OP_GREEDY: {
       
        SQRexNode *greedystop = NULL;
        SQInteger p0 = (node->right >> 16)&0x0000FFFF, p1 = node->right&0x0000FFFF, nmaches = 0;
#ifdef _DEBUG
	//printf("p0 is %d, p1 is %d\n",p0,p1);
#endif
	const SQChar *s=str;//, *good = str;
	
        if(node->next != -1)
	  {
            greedystop = &exp->_nodes[node->next];
	  }
        else
	  {
	    SQRexNode *stop_parent = &exp->_nodes[node->parent];
	    SQBool no_parent=false;
	    while(stop_parent->next==-1 || exp->_nodes[stop_parent->next].used==true)
	      {
		if(stop_parent->parent!=-1)
		  stop_parent=&exp->_nodes[stop_parent->parent];
		else
		  {		  
		    no_parent=true;
		  }
	      
	      }
	    if(no_parent==false)
	      greedystop=&exp->_nodes[stop_parent->next];
	    //greedystop =  &exp->_nodes[exp->_nodes[node->parent].next];	  
	  }

	const SQChar *stop=exp->_eol;
	const SQChar *s_temp=s;
	const SQChar *s_temp_head=s;

	if(node->left!=-1) //not dot
	  {
	    
	    while(s_temp != NULL && s_temp < exp->_eol)
	      {			
		s_temp_head=s_temp;
		s_temp=sqstd_rex_matchnode(exp,&exp->_nodes[node->left],s_temp,next,success,total_str);
		if(s_temp==NULL)break;
		
		nmaches++;		

		if(nmaches>p1)break;  //exceeds the nmaches maximum extent.
	      }
	    if(s==s_temp_head && s_temp==NULL)
	      {		
		return NULL;
	      }
	    stop=s_temp_head; //the stop is the max index satisfy class.
	    s_temp=s_temp_head;
#ifdef _DEBUG	  
	    printf("\n ================>stop is:%s\n",stop);
#endif	
	
	    if(nmaches<p0) //the maxmum of nmaches does not satisfy the minimum condition, with nmaches>p1 break, we have p0<=nmaches<=p1.
	      {		
		return NULL;
	      }
	    while(s_temp != NULL && s_temp > s)
	      {
#ifdef _DEBUG		
		printf("begin from %c\n",*s_temp);
#endif			
		if(nmaches<=p1)
		  {
		    if(greedystop)
		      {
#ifdef _DEBUG			
			if(greedystop->type>255)
			  
			  printf("begin to process greedy stop %s\n",g_nnames[greedystop->type-255]);
			else
			  printf("begin to process greedy stop %c\n",greedystop->type);
#endif				
			s_temp_head=s_temp;
			
			SQRexNode *tempgreedystop=greedystop;

		
			do {
			  if(*success==true)return *total_str;
#ifdef _DEBUG			 
			  printf("char is %c\n",*s_temp_head);
#endif				  
			  s_temp_head = sqstd_rex_matchnode(exp,tempgreedystop,s_temp_head,next,success,total_str);		  

			  if(s_temp_head==NULL)
			    {			      
			      break;
			    }
#ifdef _DEBUG
			  printf("find!,next is %c\n",*s_temp_head);
#endif			  
			  if(exp->_nodes[tempgreedystop->parent].type==258) //EXPR
			      {
				
				exp->_matches[exp->_nodes[tempgreedystop->parent].right].len = s_temp_head - exp->_matches[exp->_nodes[tempgreedystop->parent].right].begin-1;
#ifdef _DEBUG				
				printf("process the expression! right=%d, len=%d\n",exp->_nodes[tempgreedystop->parent].right,exp->_matches[exp->_nodes[tempgreedystop->parent].right].len);
#endif				
			      }
			  
			  
			  if(tempgreedystop->next!=-1)
			    {
			      tempgreedystop=&exp->_nodes[tempgreedystop->next];
			      //printf("use greedy next %c\n",tempgreedystop->type);		     
			      
			    }
			  else //if(next!=NULL)
			    {
			      SQRexNode *stop_parent = &exp->_nodes[tempgreedystop->parent];
			      while(stop_parent->next==-1 || exp->_nodes[stop_parent->next].used==true)
				{
				  if(stop_parent->parent!=-1)
				    stop_parent=&exp->_nodes[stop_parent->parent];
				  else
				    {
				      *success=true;
				      *total_str=s_temp_head;
				      return s_temp_head;///NULL;///??????
				    }
				    
				}		     
			      
			      tempgreedystop=&exp->_nodes[stop_parent->next];

			      /*
			      printf("process parent type is %d\n",stop_parent->type);
			      if(stop_parent->type==258) //EXPR
				{				  
				  exp->_matches[stop_parent->right].len = s_temp_head - exp->_matches[stop_parent->right].begin;
				}
			      */			     
			    }			 
			}while(1);		
		      }
		    else
		      {
#ifdef _DEBUG			
			printf("return1 *success=true!,begin at %c\n",*s_temp_head);
#endif			
			*success=true;
			*total_str=s_temp_head;
			return s_temp_head;
		      }		    
		  }
		s_temp--;
		nmaches--;
	
		if(nmaches<p0)break;
		
		
	      }    
		
		if(s_temp==s && p0==0)return s;
	
	  }	

	return NULL;
    }
    case OP_OR: {
      const SQChar *asd = str;      
            SQRexNode *temp=&exp->_nodes[node->left];
            while( (asd = sqstd_rex_matchnode(exp,temp,asd,NULL,success,total_str)) ) {
                if(temp->next != -1)
                    temp = &exp->_nodes[temp->next];
                else
                    return asd;
            }
            asd = str;
            temp = &exp->_nodes[node->right];
            while( (asd = sqstd_rex_matchnode(exp,temp,asd,NULL,success,total_str)) ) {
                if(temp->next != -1)
                    temp = &exp->_nodes[temp->next];
                else
                    return asd;
            }
            return NULL;
            break;
    }
    case OP_EXPR:
    case OP_NOCAPEXPR:{
            SQRexNode *n = &exp->_nodes[node->left];
            //SQChar *cur = str;
	    const SQChar *cur = str;
            SQInteger capture = -1;
            if(node->type != OP_NOCAPEXPR && node->right == exp->_currsubexp) {
	      
                capture = exp->_currsubexp;
                exp->_matches[capture].begin = cur;
		//printf("capture begin is %c\n",cur[0]);
                exp->_currsubexp++;
		//printf("capture number is %d\n",capture);
            }
            SQInteger tempcap = exp->_currsubexp;
            do {

	      /*
                SQRexNode *subnext = NULL;
                if(n->next != -1) {
                    subnext = &exp->_nodes[n->next];
                }else {
		  SQRexNode *stop_parent = &exp->_nodes[n->parent];
		  while(stop_parent->next==-1 || exp->_nodes[stop_parent->next].used==true)
		    {
		      if(stop_parent->parent!=-1)
			stop_parent=&exp->_nodes[stop_parent->parent];
		      else
			{
			  *success=true;
			  *total_str=cur;
			  return cur;///NULL;///??????
			}
		      
		    }		     
		  
		  subnext=&exp->_nodes[stop_parent->next];
		  //subnext = next;
		  
                }
	      */
		//printf("The type of the node is %s\n",g_nnames[n->type-255]);
                if(!(cur = sqstd_rex_matchnode(exp,n,cur,NULL,success,total_str))) {
		  //if(!(cur = sqstd_rex_matchnode(exp,n,cur,next,success,total_str))) {
		  if(capture != -1){
		    //printf("=============%s\n",cur);
		    exp->_matches[capture].begin = 0;
		    exp->_matches[capture].len = 0; //??
		  }
		  exp->_currsubexp = tempcap-1;
		  return NULL;
                }
		/*
		if(*success==true)
		{
		  printf("the return >>>>>>>>>>>> length is %d\n",*total_str-str);
		  return *total_str;
		}
		*/
            } while((n->next != -1) && (n = &exp->_nodes[n->next]));

            exp->_currsubexp = tempcap;
            if(capture != -1 && exp->_matches[capture].len==0)
	      {
		//if(capture == 0 && *success==true)
		//exp->_matches[capture].len = *total_str - exp->_matches[capture].begin;  //?
		//else
		
		  exp->_matches[capture].len = cur - exp->_matches[capture].begin;  //?

		  //printf("---->length of capture string is %d\n",exp->_matches[capture].len);
	      }
	    //if(capture==0 && *success==true)
	    //return *total_str;
            return cur;
    }
    case OP_WB:
        if((str == exp->_bol && !isspace(*str))
         || (str == exp->_eol && !isspace(*(str-1)))
         || (!isspace(*str) && isspace(*(str+1)))
         || (isspace(*str) && !isspace(*(str+1))) ) {
            return (node->left == 'b')?str:NULL;
        }
        return (node->left == 'b')?NULL:str;
    case OP_BOL:
        if(str == exp->_bol) return str;
        return NULL;
    case OP_EOL:
        if(str == exp->_eol) return str;
        return NULL;
    case OP_DOT:{
        if (str == exp->_eol) return NULL;
        str++;
                }
        return str;
    case OP_NCLASS:
    case OP_CLASS:
        if (str == exp->_eol) return NULL;
        if(sqstd_rex_matchclass(exp,&exp->_nodes[node->left],*str)?(type == OP_CLASS?SQTrue:SQFalse):(type == OP_NCLASS?SQTrue:SQFalse)) {
            str++;
            return str;
        }
        return NULL;
    case OP_CCLASS:
        if (str == exp->_eol) return NULL;
        if(sqstd_rex_matchcclass(node->left,*str)) {
            str++;
            return str;
        }
        return NULL;
    case OP_MB:
        {
            SQInteger cb = node->left; //char that opens a balanced expression
            if(*str != cb) return NULL; // string doesnt start with open char
            SQInteger ce = node->right; //char that closes a balanced expression
            SQInteger cont = 1;
            const SQChar *streol = exp->_eol;
	    //SQChar *streol = exp->_eol;
            while (++str < streol) {
              if (*str == ce) {
                if (--cont == 0) {
		  *success=true;
		  *total_str=str+1;
		  return ++str;
                }
              }
              else if (*str == cb) cont++;
            }
        }
        return NULL; // string ends out of balance
    default: /* char */
        if (str == exp->_eol) return NULL;
        if(*str != node->type) return NULL;
	str++;
        return str;
    }
    return NULL;
}

/* public api */
SQRex *sqstd_rex_compile(const SQChar *pattern,const SQChar **error)
{
    SQRex * volatile exp = (SQRex *)sq_malloc(sizeof(SQRex)); // "volatile" is needed for setjmp()
    exp->_eol = exp->_bol = NULL;
    exp->_p = pattern;
    //printf("the pattern is %s\n",pattern);
    exp->_nallocated = (SQInteger)scstrlen(pattern) * sizeof(SQChar);
    //printf("the nallocated is %d\n",exp->_nallocated);
    exp->_nodes = (SQRexNode *)sq_malloc(exp->_nallocated * sizeof(SQRexNode));

    //printf("the nodes size is %d\n",exp->_nallocated * sizeof(SQRexNode));
    
    exp->_nsize = 0;
    exp->_matches = 0;
    exp->_nsubexpr = 0;
    exp->_first = sqstd_rex_newnode(exp,OP_EXPR);
    exp->_error = error;
    exp->_jmpbuf = sq_malloc(sizeof(jmp_buf));
    if(setjmp(*((jmp_buf*)exp->_jmpbuf)) == 0) {
        SQInteger res = sqstd_rex_list(exp);
        exp->_nodes[exp->_first].left = res;
	exp->_nodes[res].parent = exp->_first;
        if(*exp->_p!='\0')
            sqstd_rex_error(exp,_SC("unexpected character"));
#ifdef _DEBUG
        {
            SQInteger nsize,i;
            //SQRexNode *t;
            nsize = exp->_nsize;
            //t = &exp->_nodes[0];
            scprintf(_SC("\n"));
            for(i = 0;i < nsize; i++) {
                if(exp->_nodes[i].type>MAX_CHAR)
                    scprintf(_SC("[%02d] %10s "),i,g_nnames[exp->_nodes[i].type-MAX_CHAR]);
                else
                    scprintf(_SC("[%02d] %10c "),i,exp->_nodes[i].type);
                scprintf(_SC("left %02d right %02d next %02d parent %02d\n"), (SQInt32)exp->_nodes[i].left, (SQInt32)exp->_nodes[i].right, (SQInt32)exp->_nodes[i].next, (SQInt32)exp->_nodes[i].parent);
            }
            scprintf(_SC("\n"));
        }
#endif
        exp->_matches = (SQRexMatch *) sq_malloc(exp->_nsubexpr * sizeof(SQRexMatch));
        memset(exp->_matches,0,exp->_nsubexpr * sizeof(SQRexMatch));
    }
    else{
        sqstd_rex_free(exp);
        return NULL;
    }
    return exp;
}

void sqstd_rex_free(SQRex *exp)
{
    if(exp) {
        if(exp->_nodes) sq_free(exp->_nodes,exp->_nallocated * sizeof(SQRexNode));
        if(exp->_jmpbuf) sq_free(exp->_jmpbuf,sizeof(jmp_buf));
        if(exp->_matches) sq_free(exp->_matches,exp->_nsubexpr * sizeof(SQRexMatch));
        sq_free(exp,sizeof(SQRex));
    }
}

SQBool sqstd_rex_match(SQRex* exp,const SQChar* text)
{
    const SQChar* res = NULL;
    SQBool success;
    const SQChar* total_str;
    exp->_bol = text;
    exp->_eol = text + scstrlen(text);
    exp->_currsubexp = 0;
    res = sqstd_rex_matchnode(exp,exp->_nodes,text,NULL,&success,&total_str);
    if(res == NULL || res != exp->_eol)
        return SQFalse;
    return SQTrue;
}

SQBool sqstd_rex_searchrange(SQRex* exp,const SQChar* text_begin,const SQChar* text_end,const SQChar** out_begin, const SQChar** out_end)
{
  
    const SQChar *cur = NULL;
    SQInteger node = exp->_first;
    SQBool success=false;
    const SQChar *total_str=NULL;
    //printf("text_begin is %s\n",text_begin);
    if(text_begin >= text_end) return SQFalse;
    exp->_bol = text_begin;
    exp->_eol = text_end;
    do {
        cur = text_begin;
	for(SQInteger i=0;i<exp->_nsize;i++)
	  exp->_nodes[i].used=false;

	  while(node != -1) {
            exp->_currsubexp = 0;
	    //printf("match node at %c\n",cur[0]);
	   
            cur = sqstd_rex_matchnode(exp,&exp->_nodes[node],cur,NULL,&success,&total_str);     
	    if(cur !=NULL )
	      {
		//printf("\npost %s\n",cur);
		break;
	      }
	    
            if(!cur)
                break;
            node = exp->_nodes[node].next;
	    
        }
        text_begin++;
    } while(cur == NULL && text_begin != text_end && success==false);

    if(cur == NULL)
        return SQFalse;

    text_begin--;

    
    if(out_begin)
      {
	//printf("out_begin at %s!\n",text_begin);
	*out_begin = text_begin;
      }
    if(out_end)
      {
	//printf("out_end at %s!\n",cur);
	//*out_end = cur;
	*out_end = total_str;
      }   
    
    
    return SQTrue;
}

SQBool sqstd_rex_search(SQRex* exp,const SQChar* text, const SQChar** out_begin, const SQChar** out_end)
{
  for(int i=0;i<exp->_nsubexpr;i++)
     exp->_matches[i].len = 0;	
  #ifdef _DEBUG
  printf("sqstd_rex_search executed!\n");
  #endif
    return sqstd_rex_searchrange(exp,text,text + scstrlen(text),out_begin,out_end);
}

SQInteger sqstd_rex_getsubexpcount(SQRex* exp)
{
  #ifdef _DEBUG
  printf("sqstd_rex_getsubexpcount executed!\n");
  #endif
    return exp->_nsubexpr;
}

SQBool sqstd_rex_getsubexp(SQRex* exp, SQInteger n, SQRexMatch *subexp)
{
  #ifdef _DEBUG
  printf("sqstd_rex_getsubexp executed!\n");
  #endif
    if( n<0 || n >= exp->_nsubexpr) return SQFalse;
    *subexp = exp->_matches[n];
    return SQTrue;
}

