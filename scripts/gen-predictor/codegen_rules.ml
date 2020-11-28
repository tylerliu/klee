open Core
open Ir

type if_tree = Branch of term * if_tree * if_tree
             | Leaf of int

let rec gen_if_tree branches cumul_cnd =
  let find_agreeing_branches cnd =
    List.filter ~f:(fun b -> List.exists Import.(b.cond)
                       ~f:(fun c -> term_eq c cnd))
  in
  match branches with
  | hd :: tl -> begin
    match List.find Import.(hd.cond) ~f:(fun c ->
        not (List.exists cumul_cnd ~f:(term_eq c)))
    with
    | Some pro ->
      let contra = match pro with
        | Not x -> x.v
        | x -> Not {v=x; t=Boolean}
      in
      let then_branch = gen_if_tree
          (hd::(find_agreeing_branches pro tl))
          (pro::cumul_cnd)
      in
      let else_branch = gen_if_tree
          (find_agreeing_branches contra tl)
          (contra::cumul_cnd)
      in
      Branch (pro, then_branch, else_branch)
    | None -> Leaf hd.perf
    end
  | [] -> Leaf (-1)

let rec render_if_tree tree ident debug =
  match tree with
  | Branch (c, t, e) ->
    let cond =
      if debug
      then Sexp.to_string (sexp_of_term c)
      else render_term c
    in
    ident ^ "if " ^ cond ^ ":\n" ^
    (render_if_tree t (ident ^ "    ") debug) ^ "\n" ^
    ident ^ "else:\n" ^
    (render_if_tree e (ident ^ "    ") debug)
  | Leaf x -> ident ^ "return " ^ (string_of_int x)

let rewrite_cond cond =
  List.fold_left ~init:cond ~f:(fun cond rule ->
      call_recursively_on_term rule cond)
    Program_rules.rewrite_rules

let rec rewrite_if_tree = function
  | Branch (c, t, e) ->
    (* print_endline (Sexp.to_string (Ir.sexp_of_term c)); *)
    Branch ((rewrite_cond {v=c;t=Boolean}).v,
            rewrite_if_tree t, rewrite_if_tree e)
  | Leaf x -> Leaf x

let gen_predictor branches debug =
  render_if_tree (rewrite_if_tree (gen_if_tree branches [])) "" debug

let convert_constraints_to_predictor in_fname out_fname debug =
  match Sys.file_exists in_fname with
  | `No | `Unknown -> failwith ("Source constraints faile " ^ out_fname ^ "does not exist")
  | `Yes -> begin
      print_endline ("generating predictor code in " ^
                     out_fname ^ " based on constraints in " ^
                     in_fname);
      let constraints = In_channel.read_all in_fname in
      let branches = Import.parse_branches constraints in
      let predictor = gen_predictor branches debug in
      Out_channel.write_all out_fname ~data:predictor
    end