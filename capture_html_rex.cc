local argv_number=vargv.len();

//print(argv_number+"\n");

/*
if(argv_number!=2 && argv_number!=4)
  {
    print("Input html file and regular expression to fetch!\n");
    return 1;
  }
*/

local file_content = "";
local f=file(vargv[0],"rb");
while(!f.eos())
  {
    file_content += f.readn('b').tochar();
  }
f.close()

local reg = regexp(vargv[1]);
/*
local reg_start;
local reg_stop;

if(argv_number==4)
  {
    reg_start = regexp(vargv[2]);
    reg_stop = regexp(vargv[3]);
  }
*/
local print_begin=0;
local count=0;
local start_index=0;
while(1)
  {
    local res = reg.capture(file_content,start_index);
    if(!res)break;
    for(local i=2;i<argv_number;i++)
      {
	print(file_content.slice(res[vargv[i].tointeger()].begin,res[vargv[i].tointeger()].end)+"    ");
      }
    print("\n");	

    start_index=res[0].end+1;
    
    count++;
    if(count==500)break;
      
  }

