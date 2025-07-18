open Core
open Gen_predictor_lib

let () =
  let argv = Sys.get_argv () in
  let fname = argv.(1) in
  let debug =
    ((Array.length argv) = 3) &&
    (String.equal argv.(2) "-debug")
  in
  let out_fname = fname ^ ".py" in
  Codegen_rules.convert_constraints_to_predictor fname out_fname debug
