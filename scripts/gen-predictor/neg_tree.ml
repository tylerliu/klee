open Core
open Gen_predictor_lib

type tbranch = {cond : Ir.tterm list; perf : string}

let () =
  let argv = Sys.get_argv () in
  let ip_file = argv.(1) in
  let constraints = In_channel.read_all ip_file in
  let branches = Import.parse_branches constraints in
  let final_branches: tbranch list = List.map branches ~f:(fun my_tuple -> 
        let rewritten_conds = List.map my_tuple.cond ~f:( fun condn -> Codegen_rules.rewrite_cond {v=condn; t=Unknown} ) in
        {cond=rewritten_conds;perf=my_tuple.perf} )
   in
   List.iter final_branches ~f:(fun br -> 
    let final_str_branches = List.map  br.cond ~f:(fun condn -> Ir.render_tterm condn) in
    List.iter final_str_branches ~f:(fun str_br -> Printf.printf "%s\t" str_br);
    Printf.printf ",%s\n" br.perf )