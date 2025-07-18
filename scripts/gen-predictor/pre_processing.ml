open Core
open Gen_predictor_lib


let () =
  let argv = Sys.get_argv () in
  let ip_file = argv.(1) in
  let ip_file_lines = In_channel.read_all ip_file in
  let branches_raw = Str.split (Str.regexp "\n+") ip_file_lines in
  let branches =  List.map branches_raw ~f:(fun exp ->
    Ir.render_tterm
      (Codegen_rules.rewrite_cond(Import.parse_condition exp))) in
  List.iter branches ~f:(fun py_exp -> Printf.printf "%s\n" py_exp)
