import re
import os
import sys

port_list_file = sys.argv[1]
symbol_re = re.compile(".*Insert program specific rules here.*")
port_list={}

def get_script_path():
    return os.path.dirname(os.path.realpath(sys.argv[0]))

def main():
  script_dir = get_script_path()
  domain_rules_file = script_dir+"/domain.ml"
  output_file = script_dir+"/program_rules.ml"

  with open(port_list_file,'r') as plist:
    for line in plist:
      text = line.rstrip().split(' ')
      port_list[text[0]]=text[1]
  
  template_part1 = "\t\t\t| Bop(Eq,\n\t\t\t\t\t\t\t{v = Int "
  template_part2 = "; t =_},\n\t\t\t\t\t\t\t{v = Utility (Slice ({v=Id \"VIGOR_DEVICE\";t=_}, 0, 32)); t= _})\n"
  template_part3 = "\t\t\t\t\t->Some (Bop (Eq,\n\t\t\t\t\t\t\t\t\t\t\t{v = Str_idx({v = Id \"pkt\"; t = Unknown},\"port\"); t = Unknown},\n"
  template_part4 = "\t\t\t\t\t\t\t\t\t\t\t{v = Id \""
  template_part5 = "\"; t = Unknown}))"

  with open(domain_rules_file, 'r') as ip, open(output_file,'w') as op:
    for line in ip:
      op.write(line.rstrip()+"\n")
      if symbol_re.match(line):
        for port,name in port_list.items():
          rule=template_part1+port+template_part2+template_part3+template_part4+name+template_part5
          op.write(rule+"\n")

if __name__ == "__main__":
    main()