open Core
open Ir

type branch = {cond : term list; perf : int}

let do_log = false

let log str =
  if do_log then Out_channel.with_file ~append:true "parser.log" ~f:(fun f ->
      Out_channel.output_string f str)
  else ()

let lprintf fmt = ksprintf log fmt

type 'x confidence = Sure of 'x | Tentative of 'x | Noidea

type t_width = W1 | W8 | W16 | W32 | W48 | W64
type t_sign = Sgn | Unsgn
type type_guess = {w:t_width confidence;
                   s:t_sign confidence;
                   precise: ttype}

type typed_var = {vname:string; t: type_guess;}

type moment = Beginning | After of int

type address_spec = {value:tterm;
                     callid:moment;
                     str_depth:int;
                     tt:ttype;
                     breakdown:int64 String.Map.t}

type guessed_types = {ret_type: ttype;
                      arg_types: ttype list;
                      extra_ptr_types: ttype String.Map.t}

let known_addresses : address_spec list Int64.Map.t ref = ref Int64.Map.empty

let int_of_twidth = function
    W1 -> 1 | W8 -> 8 | W16 -> 16 | W32 -> 32 | W48 -> 48 | W64 -> 64

let ttype_of_guess = function
  | {precise=Unknown;s=Tentative Sgn;w;}
  | {precise=Unknown;s=Sure Sgn;w;} -> begin match w with
      | Noidea -> Sunknown
      | Sure W1 | Tentative W1 -> Boolean
      | Sure W8 | Tentative W8 -> Sint8
      | Sure W16 | Tentative W16 -> Sint16
      | Sure W32 | Tentative W32 -> Sint32
      | Sure W48 | Tentative W48 -> Sint48
      | Sure W64 | Tentative W64 -> Sint64
      end
  | {precise=Unknown;s=Tentative Unsgn;w;}
  | {precise=Unknown;s=Sure Unsgn;w;} -> begin match w with
      | Noidea -> Uunknown
      | Sure W1 | Tentative W1 -> Boolean
      | Sure W8 | Tentative W8 -> Uint8
      | Sure W16 | Tentative W16 -> Uint16
      | Sure W32 | Tentative W32 -> Uint32
      | Sure W48 | Tentative W48 -> Uint64
      | Sure W64 | Tentative W64 -> Uint64
      end
  | {precise=Unknown;s=Noidea;w=Sure W1;}
  | {precise=Unknown;s=Noidea;w=Tentative W1;} -> Boolean
  | {precise=Unknown;s=Noidea;w=_;} -> Unknown
  | {precise;s=_;w=_} -> precise

let parse_int str =
  (* As a hack: handle -10 in 64bits.
     TODO: handle more generally*)
  if (String.equal str "18446744073709551606") then Some (-10)
  (* -10000 *)
  else if (String.equal str "18446744073709541616") then Some (-10000)
  (* -3600000000000 *)
  else if (String.equal str "18446740473709551616") then Some (-3600000000000)
  (* As another hack: handle -300 in 64bits. *)
  else if (String.equal str "18446744073709551316") then Some (-300)
  else if (String.equal str "18446744073709551556") then Some (-60)
  else if (String.equal str "18446744073709551596") then Some (-20)
  else if (String.equal str "18446744063709551616") then Some (-10000000000)
  else if (String.equal str "18446744073709551562") then Some (-54)
  else
    try Some (int_of_string str)
    with _ -> None

let is_int str = match parse_int str with Some _ -> true | None -> false

(* TODO: elaborate. *)
let guess_type exp t =
  match t with
  | Uunknown -> begin match exp with
      | Sexp.List [Sexp.Atom w; _] when w = "w8" -> Uint8
      | Sexp.List [Sexp.Atom w; _] when w = "w16" -> Uint16
      | Sexp.List [Sexp.Atom w; _] when w = "w32" -> Uint32
      | Sexp.List [Sexp.Atom w; _] when w = "w64" -> Uint64
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w8" :: _) -> Uint8
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w16" :: _) -> Uint16
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w32" :: _) -> Uint32
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w64" :: _) -> Uint64
      | _ -> failwith ("GUESS TYPE FAILURE UUnknown " ^ (Sexp.to_string exp))
    end
  | Sunknown -> begin match exp with
      | Sexp.List [Sexp.Atom w; _] when w = "w8" -> Sint8
      | Sexp.List [Sexp.Atom w; _] when w = "w16" -> Sint16
      | Sexp.List [Sexp.Atom w; _] when w = "w32" -> Sint32
      | Sexp.List [Sexp.Atom w; _] when w = "w64" -> Sint64
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w8" :: _) -> Sint8
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w16" :: _) -> Sint16
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w32" :: _) -> Sint32
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w64" :: _) -> Sint64
      | _ -> failwith ("GUESS TYPE FAILURE SUnknown " ^ (Sexp.to_string exp))
    end
  | Unknown ->  begin match exp with
      | Sexp.Atom f when f = "false" || f = "true" -> Boolean
      | Sexp.List [Sexp.Atom w; Sexp.Atom f] when w = "w32" && f = "0" ->
        lprintf "GUESS TYPE BOOL\n"; Boolean
      | Sexp.List [Sexp.Atom w; Sexp.Atom v] when w = "w8" ->
        if (is_int v) then Sint8 else Uint8
      | Sexp.List [Sexp.Atom w; Sexp.Atom v] when w = "w16" ->
        if (is_int v) then Sint16 else Uint16
      | Sexp.List [Sexp.Atom w; Sexp.Atom v] when w = "w32" ->
        if (is_int v) then Sint32 else Uint32
      | Sexp.List [Sexp.Atom w; Sexp.Atom v] when w = "w64" ->
        if (is_int v) then Sint64 else Uint64
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w8" :: _) -> Uint8
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w16" :: _) -> Uint16
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w32" :: _) -> Uint32
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w48" :: _) -> Uint48
      | Sexp.List (Sexp.Atom _ :: Sexp.Atom "w64" :: _) -> Uint64
      | _ -> failwith ("GUESS TYPE FAILURE Unknown " ^ (Sexp.to_string exp))
    end
  | _  -> t

(* TODO: this should spit out a type to help the validator *)
let int_str_of_sexp value =
  let str = Sexp.to_string value in
  let prefix = String.sub str ~pos:0 ~len:3 in
  if prefix = "(w8" then
    String.sub str ~pos:4 ~len:((String.length str - 5))
  else if prefix = "(w1" || prefix = "(w3" || prefix = "(w6" then
    (* 16, 32, 64 *)
    String.sub str ~pos:5 ~len:((String.length str - 6))
  else str

let infer_type_sign f =
  if String.equal f "Sle" then Sure Sgn
  else if String.equal f "Slt" then Sure Sgn
  else if String.equal f "Ule" then Sure Unsgn
  else if String.equal f "Ult" then Sure Unsgn
  else if String.equal f "Eq" then Sure Unsgn
  else Noidea

let expand_shorted_sexp sexp =
  let rec remove_defs exp =
    let rec do_list lst =
      match lst with
      | Sexp.Atom v :: tl when String.is_suffix v ~suffix:":" ->
        do_list tl
      | hd :: tl -> (remove_defs hd) :: (do_list tl)
      | [] -> []
    in
    match exp with
    | Sexp.List lst -> Sexp.List (do_list lst)
    | Sexp.Atom _ -> exp
  in
  let rec get_defs sexp =
    let merge_defs d1 d2 =
      String.Map.merge d1 d2
        ~f:(fun ~key pres ->
            ignore key;
            match pres with
            | `Both (_,_) -> failwith "double definition"
            | `Left x -> Some x
            | `Right x -> Some x)
    in
    let rec do_list lst =
      match lst with
      | Sexp.Atom v :: def :: tl when String.is_suffix v ~suffix:":" ->
        merge_defs (get_defs def) (String.Map.add_exn (do_list tl)
                                     ~key:(String.prefix v
                                             ((String.length v) - 1))
                                     ~data:(remove_defs def))
      | hd :: tl -> merge_defs (get_defs hd) (do_list tl)
      | [] -> String.Map.empty
    in
    match sexp with
    | Sexp.List lst -> do_list lst
    | Sexp.Atom _ -> String.Map.empty
  in
  let rec expand_exp exp vars =
    match exp with
    | Sexp.List lst ->
      let (expaneded_lst,smth_changed) =
        List.fold_left lst ~init:([],false)
          ~f:(fun (prev_expanded,prev_changed) el ->
              let (expanded,changed) = expand_exp el vars in
              (expanded::prev_expanded,changed||prev_changed))
      in
      (Sexp.List (List.rev expaneded_lst),smth_changed)
    | Sexp.Atom str -> match String.Map.find vars str with
      | Some ex -> (ex,true)
      | None -> (exp,false)
  in
  let expand_map map defs =
    let new_map_expanded =
      List.map (Map.to_alist map) ~f:(fun (name,el) ->
          (name, expand_exp el defs))
    in
    let changed =
      List.exists (List.map new_map_expanded ~f:(Fn.compose snd snd)) ~f:Fn.id
    in
    let new_map =
      String.Map.of_alist_exn (List.map new_map_expanded
                                 ~f:(fun (name,(new_el,_)) -> (name,new_el)))
    in
    (new_map,changed)
  in
  let rec cross_expand_defs_fixp defs =
    let (new_defs, expanded) = expand_map defs defs in
    if expanded then cross_expand_defs_fixp new_defs
    else defs
  in
  let map_expandable map defs =
    List.exists (String.Map.data map) ~f:(fun el -> (snd (expand_exp el defs)))
  in
  let defs = get_defs sexp in
  let defs = cross_expand_defs_fixp defs in
  if (map_expandable defs defs) then begin
    lprintf "failed expansion on sexp: \n%s\n" (Sexp.to_string sexp);
    lprintf "defs: ";
    Map.iteri (get_defs sexp) ~f:(fun ~key ~data ->
        lprintf "%s ::: %s\n" key (Sexp.to_string data));
    lprintf "expanded defs: ";
    Map.iteri defs ~f:(fun ~key ~data ->
        lprintf "%s ::: %s\n" key (Sexp.to_string data));
    failwith ("incomplete map expansion for " ^ (Sexp.to_string sexp));
  end;
  (fst (expand_exp (remove_defs sexp) defs))

let to_symbol str =
  let no_spaces = String.substr_replace_all str ~pattern:" " ~with_:"_" in
  let no_octotorps =
    String.substr_replace_all no_spaces ~pattern:"#" ~with_:"num"
  in
  no_octotorps

let convert_str_to_width_confidence w =
  if String.equal w "w1" then Sure W1
  else if String.equal w "w8" then Sure W8
  else if String.equal w "w16" then Sure W16
  else if String.equal w "w32" then Sure W32
  else if String.equal w "w48" then Sure W48
  else if String.equal w "w64" then Sure W64
  else Noidea

let get_slice_of_sexp exp t =
  match exp with
  | Sexp.List [Sexp.Atom rd; Sexp.Atom w; Sexp.List [Sexp.Atom _;
                                                     Sexp.Atom pos];
               Sexp.Atom name]
    when ( String.equal rd "ReadLSB" ||
           String.equal rd "Read") -> begin
      match convert_str_to_width_confidence w with
      | Sure w -> Some (Utility (Slice ({v=Id name;t},
                                        8* (int_of_string pos), (* convert offset to bits, to match width *)
                                        int_of_twidth w)))
      | _ -> None
    end
  | _ -> None

let get_read_width_of_sexp exp =
  match exp with
  | Sexp.List [Sexp.Atom rd; Sexp.Atom w; Sexp.List _; Sexp.Atom _]
    when (String.equal rd "ReadLSB" ||
          String.equal rd "Read") -> Some w
  | _ -> None

let sexp_is_of_this_width sexp w =
  match get_read_width_of_sexp sexp with
  | Some ww -> String.equal ww w
  | _ -> false

let rec canonicalize_sexp sexp =
  match expand_shorted_sexp sexp with
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom w; Sexp.Atom "0";
               Sexp.List [Sexp.Atom "ZExt"; Sexp.Atom _; arg];]
    when sexp_is_of_this_width arg w ->
    lprintf "canonicalized: %s\n" (Sexp.to_string arg);
    (* TODO: make sure no sign-magic breaks here *)
    canonicalize_sexp arg
  | Sexp.List (Sexp.Atom f :: args) ->
    Sexp.List (Sexp.Atom f :: List.map args ~f:canonicalize_sexp)
  | _ -> sexp

let is_bool_fun fname =
  if String.equal fname "Eq" then true
  else if String.equal fname "Sle" then true
  else if String.equal fname "Slt" then true
  else if String.equal fname "Ule" then true
  else if String.equal fname "Ult" then true
  else false

let name_gen prefix = object
  val mutable cnt = 0
  method generate =
    cnt <- cnt + 1 ;
    prefix ^ Int.to_string cnt
end

let complex_val_name_gen = name_gen "cmplx"
let allocated_complex_vals : var_spec String.Map.t ref = ref String.Map.empty

let get_sint_in_bounds v =
  (*Special case for 64bit -10, for now,
    FIXME: make a more general case.*)
  if (String.equal v "18446744073709551606") then -10
  (* -10000 *)
  else if (String.equal v "18446744073709541616") then -10000
  (* -3600000000000 *)
  else if (String.equal v "18446740473709551616") then -3600000000000
  (* also -300 *)
  else if (String.equal v "18446744073709551316") then -300
  (* and -60 *)
  else if (String.equal v "18446744073709551556") then -60
  (* and -20 *)
  else if (String.equal v "18446744073709551596") then -20
  (* and -10 000 000 000 in 128bit *)
  else if (String.equal v "18446744063709551616") then -10000000000
  else if (String.equal v "18446744073709551562") then -54
  else
    let integer_val = Int.of_string v in
    if Int.(integer_val <> 10000000000) && (* We want this 10B - the policer exp time*)
       Int.(integer_val <>  3750000000) &&
       Int.(integer_val  >  2147483647) then
      integer_val - 2*2147483648
    else
      integer_val

let make_cmplx_val exp t =
  let key = int_str_of_sexp exp in
  match String.Map.find !allocated_complex_vals key with
  | Some spec -> {v=Id spec.name;t=spec.value.t}
  | None ->
    let name = complex_val_name_gen#generate in
    lprintf "CMPLX NAME: %s SEXP: %s TYPE: %s\n"
      name (Sexp.to_string exp) (ttype_to_str t);
    let value = {v=Id key;t} in
    allocated_complex_vals :=
      String.Map.add_exn !allocated_complex_vals ~key
        ~data:{name;value};
    {v=Id name;t}

let rec is_bool_expr exp =
  match exp with
  | Sexp.List [Sexp.Atom f; _; _] when is_bool_fun f -> true
  | Sexp.List [Sexp.Atom "And"; _; lhs; rhs] ->
    (*FIXME: and here, but really that is a bool expression, I know it*)
    (is_bool_expr lhs) || (is_bool_expr rhs)
  | Sexp.List [Sexp.Atom "ZExt"; _; e] ->
    is_bool_expr e
  | _ -> false

let rec guess_type_l exps t =
  match exps with
  | hd :: tl -> begin match guess_type hd t with
      | Unknown | Sunknown | Uunknown -> guess_type_l tl t
      | s -> s
    end
  | [] -> Unknown

let find_first_known_address_comply addr tt at property =
  let legit_candidates lst =
    List.filter lst ~f:(fun x ->
        (match x.callid, at with
         | Beginning, _ -> true
         | After _, Beginning -> false
         | After id, After searched_for -> id <= searched_for)
        &&
        (match x.tt, tt with
         | _, Unknown
         | _, Void ->
         (* TODO: should not really occur.
                         but left here for the sake of void** output pointers,
                         that are not concretized yet. *)
          failwith ("Searching for a void instantiation of addr" ^
                    (Int64.to_string addr) ^ " x.tt:" ^ (ttype_to_str x.tt) ^
                    " tt:" ^ (ttype_to_str tt))
         | Ptr ptee1, Array ptee2 ->
           if (ptee1 <> ptee2) then
             lprintf "discarding: %s * != %s []\n"
               (ttype_to_str ptee1) (ttype_to_str ptee2);
           ptee1 = ptee2
         | t1, t2 ->
           if (t1 <> t2) then
             lprintf "discarding: %s != %s\n"
               (ttype_to_str t1) (ttype_to_str t2);
           t1 = t2)
        &&
        (property x))
  in
  let find_the_right candidates =
    List.reduce ~f:(fun prev cand ->
        match prev.callid, cand.callid with
        | Beginning, _ -> prev
        | _, Beginning -> cand
        | After x1, After x2 ->
          if x1 < x2 then prev
          else if x2 < x1 then cand
          else if prev.str_depth < cand.str_depth then prev
          else cand)
      candidates
  in
  Option.bind (Int64.Map.find !known_addresses addr)
    ~f:(fun lst ->
       Option.map ~f:(fun addr_sp -> addr_sp.value)
         (find_the_right (legit_candidates lst)))

let moment_to_str = function
  | Beginning -> "<|"
  | After x -> ("> " ^ (string_of_int x))

let find_first_known_address addr tt at =
  lprintf "looking for first %Ld : %s at %s\n"
    addr (ttype_to_str tt) (moment_to_str at);
  find_first_known_address_comply addr tt at (fun _ -> true)

let find_first_known_address_or_dummy addr t at =
  match find_first_known_address addr t at with
  | Some tt -> tt
  | None -> {v=Utility (Ptr_placeholder addr); t=Ptr t}

let make_cast_if_needed tt srct dstt =
  if srct = dstt then tt
  else if srct = Uint32 && dstt = Uint16 then
    {v=Cast(dstt, {v=Bop(Bit_and, tt, {v=Int 0xFFFF;t=Uint32});t=Uint32});
     t=dstt}
  else {v=Cast(dstt, tt);t=dstt}

let rec get_sexp_value_raw exp ?(at=Beginning) t =
  lprintf "SEXP %s : %s\n" (Sexp.to_string exp) (ttype_to_str t);
  let exp = canonicalize_sexp exp in
  let t = match t with
    |Unknown|Sunknown|Uunknown -> guess_type exp t
    |_ -> t
  in
  let exp = match exp with
    | Sexp.List [Sexp.Atom w; Sexp.Atom f]
      when w = "w8" || w = "w16" || w = "w32" || w = "w64" -> Sexp.Atom f
    | _ -> exp
  in
  match exp with
  | Sexp.Atom v ->
    begin
      match t with
      | Sint64 | Sint48 | Sint32 | Sint16 | Sint8 -> {v=Int (get_sint_in_bounds v);t}
      | _ ->
        if String.equal v "true" then {v=Bool true;t=Boolean}
        else if String.equal v "false" then {v=Bool false;t=Boolean}
        else if String.equal v "18446744073709551611" then {v=Int (-5);t=Sint64}
        (*FIXME: deduce the true integer type for the value: *)
        else begin match parse_int v with
          | Some n -> let addr = (Int64.of_int n) in
                      if addr = 0L then {v=Int 0; t}
                      else
                        begin match t with
                        | Ptr x -> find_first_known_address_or_dummy addr x at
                        | _ -> {v=Int n;t} end
          | None -> {v=Id v;t} end
    end
  (* Hardcode this because VeriFast doesn't like bit ors
     false == (0 == (a | b))
     ==>
     false == (a == 0 && b == 0)
     ==>
     a || b *)
  | Sexp.List [Sexp.Atom "Eq"; Sexp.Atom "false";
               Sexp.List [Sexp.Atom "Eq";
                          Sexp.List [Sexp.Atom "w32"; Sexp.Atom "0"];
                          Sexp.List [Sexp.Atom "Or"; Sexp.Atom "w32";
                                     Sexp.List [Sexp.Atom "ZExt";
                                                Sexp.Atom "w32"; left];
                                     Sexp.List [Sexp.Atom "ZExt";
                                                Sexp.Atom "w32"; right];
                                    ];
                         ];
              ] ->
    {v=Bop(Or,
           get_sexp_value_raw left Boolean ~at,
           get_sexp_value_raw right Boolean ~at);t=Boolean}
  | Sexp.List [Sexp.Atom "Shl"; Sexp.Atom "w32"; target; shift;] ->
    {v=Bop(Shl,
           (get_sexp_value_raw target Uint32 ~at),
           (get_sexp_value_raw shift Uint32 ~at));t}
  | Sexp.List [Sexp.Atom "AShr"; Sexp.Atom "w32"; target; shift;] ->
    {v=Bop(AShr,
           (get_sexp_value_raw target Uint32 ~at),
           (get_sexp_value_raw shift Uint32 ~at));t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w8"; Sexp.Atom "0"; src;] ->
    get_sexp_value_raw src t ~at
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w16"; Sexp.Atom "0"; src;]
    when t = Uint16 ->
    let srct = (guess_type src Uunknown) in
    make_cast_if_needed (get_sexp_value_raw src srct ~at) srct t
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w16"; Sexp.Atom "0"; src;]
    when t = Sint16 ->
    let srct = (guess_type src Sunknown) in
    make_cast_if_needed (get_sexp_value_raw src srct ~at) srct t
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w16"; Sexp.Atom "0"; src;]
    when t = Uint64 ->
    let srct = (guess_type src Uunknown) in
    {v=Cast(t,{v=Cast(Uint16, (get_sexp_value_raw src srct ~at));t=Uint16});t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w16"; Sexp.Atom "0"; src;]
    when t = Sint64 ->
    let srct = (guess_type src Sunknown) in
    {v=Cast(t,{v=Cast(Sint16, (get_sexp_value_raw src srct ~at));t=Sint16});t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w16"; Sexp.Atom "0"; src;]
    when t = Uint32 ->
    let srct = (guess_type src Uunknown) in
    {v=Cast(t,{v=Cast(Uint16, (get_sexp_value_raw src srct ~at));t=Uint16});t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w16"; Sexp.Atom "0"; src;]
    when t = Sint32 ->
    let srct = (guess_type src Sunknown) in
    {v=Cast(t,{v=Cast(Sint16, (get_sexp_value_raw src srct ~at));t=Sint16});t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w32"; Sexp.Atom "0"; src;]
    when t = Uint32 ->
    let srct = (guess_type src Uunknown) in
    make_cast_if_needed (get_sexp_value_raw src srct ~at) srct t
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w32"; Sexp.Atom "0"; src;]
    when t = Sint32 ->
    let srct = (guess_type src Sunknown) in
    make_cast_if_needed (get_sexp_value_raw src srct ~at) srct t
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w32"; Sexp.Atom "0"; src;]
    when t = Uint64 ->
    let srct = (guess_type src Uunknown) in
    {v=Cast(t,{v=Cast(Uint32, (get_sexp_value_raw src srct ~at));t=Uint32});t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w32"; Sexp.Atom "0"; src;]
    when t = Sint64 ->
    let srct = (guess_type src Sunknown) in
    {v=Cast(t,{v=Cast(Sint32, (get_sexp_value_raw src srct ~at));t=Sint32});t}
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w64"; Sexp.Atom "0"; src;]
    when t = Uint64 ->
    let srct = (guess_type src Uunknown) in
    make_cast_if_needed (get_sexp_value_raw src srct ~at) srct t
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "w64"; Sexp.Atom "0"; src;]
    when t = Sint64 ->
    let srct = (guess_type src Sunknown) in
    make_cast_if_needed (get_sexp_value_raw src srct ~at) srct t
  | Sexp.List [Sexp.Atom "Extract"; Sexp.Atom "0"; src;] ->
    get_sexp_value_raw src Boolean ~at
  | Sexp.List [Sexp.Atom "SExt"; Sexp.Atom "w64"; arg] ->
    {v=Cast(Uint64,get_sexp_value_raw arg Uint32 ~at);t=Uint64}
  | Sexp.List [Sexp.Atom "Mul"; Sexp.Atom _; lhs; rhs] ->
    let mt = guess_type_l [lhs;rhs] Unknown in
    {v=Bop(Mul,
           get_sexp_value_raw lhs mt ~at,
           get_sexp_value_raw rhs mt ~at);t=mt}
  | Sexp.List [Sexp.Atom "Sub"; Sexp.Atom _; lhs; rhs] ->
    let mt = guess_type_l [lhs;rhs] Unknown in
    {v=Bop(Sub,
           get_sexp_value_raw lhs mt ~at,
           get_sexp_value_raw rhs mt ~at);t=mt}
  | Sexp.List [Sexp.Atom "Add"; Sexp.Atom _; lhs; rhs] ->
    let res_type = guess_type_l [lhs;rhs] Unknown in
    let res_type = if is_unknown res_type then t else res_type in
    let expression =
      begin (* Prefer a variable in the left position
             due to the weird VeriFast type inference rules.*)
        match lhs with
        | Sexp.Atom str ->
          begin match parse_int str with
            | Some n -> {v=Bop (Sub,
                                (get_sexp_value_raw rhs res_type ~at),
                                {v=(Int n);t=res_type});t=res_type}
            | _ -> {v=Bop (Add,
                           (get_sexp_value_raw lhs res_type ~at),
                           (get_sexp_value_raw rhs res_type ~at));t=res_type}
          end
        | _ -> {v=Bop (Add,
                       (get_sexp_value_raw lhs res_type ~at),
                       (get_sexp_value_raw rhs res_type ~at));t=res_type}
      end
    in
    make_cast_if_needed expression expression.t t
  | Sexp.List [Sexp.Atom f; lhs; rhs]
    when (String.equal f "Slt") ->
    (*FIXME: get the actual type*)
    let ty = guess_type_l [lhs;rhs] Sunknown in
    {v=Bop (Lt,
            (get_sexp_value_raw lhs ty ~at),
            (get_sexp_value_raw rhs ty ~at));t}
  | Sexp.List [Sexp.Atom f; lhs; rhs]
    when (String.equal f "Sle") ->
    (*FIXME: get the actual type*)
    {v=Bop (Le,
            (get_sexp_value_raw lhs Sunknown ~at),
            (get_sexp_value_raw rhs Sunknown ~at));t}
  | Sexp.List [Sexp.Atom f; lhs; rhs]
    when (String.equal f "Ule") ->
    (*FIXME: get the actual type*)
    {v=Bop (Le,
            (get_sexp_value_raw lhs Uunknown ~at),
            (get_sexp_value_raw rhs Uunknown ~at));t}
  | Sexp.List [Sexp.Atom "Ult";
               lhs;
               Sexp.List [Sexp.Atom "w64"; Sexp.Atom "10"]] ->
    (* HACK: this is the time being compared to 10LL, so put LL there *)
    {v=Bop (Lt,(get_sexp_value_raw lhs Sunknown ~at), {v=Int 10;t=Sint64});t}
  | Sexp.List [Sexp.Atom f; lhs; rhs]
    when (String.equal f "Ult") ->
    {v=Bop (Lt,
            (get_sexp_value_raw lhs Uunknown ~at),
            (get_sexp_value_raw rhs Uunknown ~at));t}
  | Sexp.List [Sexp.Atom "Eq";
               Sexp.List [Sexp.Atom "w32"; Sexp.Atom "0"];
               Sexp.List ((Sexp.Atom "ReadLSB" :: tl))] ->
    {v=Bop (Eq,
            {v=Int 0;t=Uint32},
            (get_sexp_value_raw
               (Sexp.List (Sexp.Atom "ReadLSB" :: tl)) Uint32 ~at));
     t=Boolean}
  | Sexp.List [Sexp.Atom f; lhs; rhs]
    when (String.equal f "Eq") ->
    let ty = guess_type_l [lhs;rhs] Unknown in
    {v=Bop (Eq,
            (get_sexp_value_raw lhs ty ~at),
            (get_sexp_value_raw rhs ty ~at));t}
  | Sexp.List [Sexp.Atom f; _; e]
    when String.equal f "ZExt" ->
    (*TODO: something smarter here.*)
    get_sexp_value_raw e t ~at
  | Sexp.List [Sexp.Atom f; Sexp.Atom _; lhs; rhs]
    when (String.equal f "And") &&
         ((is_bool_expr lhs) || (is_bool_expr rhs)) ->
    (*FIXME: and here, but really that is a bool expression, I know it*)
    (*TODO: check t is really Boolean here*)
    {v=Bop (And,
            (get_sexp_value_raw lhs Boolean ~at),
            (get_sexp_value_raw rhs Boolean ~at));t}
  | Sexp.List [Sexp.Atom "Or"; Sexp.Atom _; lhs; rhs]
    when ((is_bool_expr lhs) || (is_bool_expr rhs)) ->
    (*FIXME: or here, but really that is a bool expression, I know it*)
    (*TODO: check t is really Boolean here*)
    {v=Bop (Or,
            (get_sexp_value_raw lhs Boolean ~at),
            (get_sexp_value_raw rhs Boolean ~at));t}
  | Sexp.List [Sexp.Atom "Or"; Sexp.Atom "w32"; lhs; rhs] ->
    {v=Bop (Bit_or,
            (get_sexp_value_raw lhs Uint32 ~at),
            (get_sexp_value_raw rhs Uint32 ~at));t}
  | Sexp.List [Sexp.Atom "Or"; Sexp.Atom "w16"; lhs; rhs] ->
  {v=Bop (Bit_or,
            (get_sexp_value_raw lhs Uint16 ~at),
            (get_sexp_value_raw rhs Uint16 ~at));t}
  | Sexp.List [Sexp.Atom "And"; Sexp.Atom _; lhs; rhs] ->
    begin 
      match rhs with
      | Sexp.List [Sexp.Atom "w32"; Sexp.Atom n] when is_int n ->
        if t = Boolean then
          {v=Bop (Eq,
                  (get_sexp_value_raw rhs Uint32 ~at),
                  {v=Bop (Bit_and,
                          (get_sexp_value_raw lhs Uint32 ~at),
                          (get_sexp_value_raw rhs Uint32 ~at));t=Uint32});
           t=Boolean}
        else
          {v=Bop (Bit_and,
                  (get_sexp_value_raw lhs Uint32 ~at),
                  (get_sexp_value_raw rhs Uint32 ~at));t=Uint32}
      | _ ->
        let ty = guess_type_l [lhs;rhs] t in
        lprintf "interesting And case{%s}: %s "
          (ttype_to_str ty) (Sexp.to_string exp);
        if ty = Boolean then
          {v=Bop (And,
                  (get_sexp_value_raw lhs ty ~at),
                  (get_sexp_value_raw rhs ty ~at));t=ty}
        else
          {v=Bop (Bit_and,
                  (get_sexp_value_raw lhs ty ~at),
                  (get_sexp_value_raw rhs ty ~at));t=ty}
    end
  | Sexp.List [Sexp.Atom f; Sexp.Atom _; Sexp.Atom lhs; rhs;]
    when (String.equal f "Concat") && (String.equal lhs "0") ->
    get_sexp_value_raw rhs t ~at
  | Sexp.List [Sexp.Atom "Select";
               Sexp.Atom _; nested;
               Sexp.List [Sexp.Atom _; Sexp.Atom "1"];
               Sexp.List [Sexp.Atom _; Sexp.Atom "0"]] ->
    (* This is equivalent to x ? 1 : 0 ==> we just pretend x is a boolean *)
    get_sexp_value_raw nested Boolean ~at
  | Sexp.List [Sexp.Atom "SRem"; Sexp.Atom width; value; divisor] ->
    let guess = {precise=Unknown;
                 s=Sure Sgn;
                 w=convert_str_to_width_confidence width} in
    let mt = ttype_of_guess guess in
    {v=Bop(Modulo,
           get_sexp_value_raw value mt ~at,
           get_sexp_value_raw divisor mt ~at);t=mt}
  | Sexp.List [Sexp.Atom "UDiv"; Sexp.Atom width; value; divisor] ->
    let guess = {precise=Unknown;
                 s=Sure Unsgn;
                 w=convert_str_to_width_confidence width} in
    let mt = ttype_of_guess guess in
    {v=Bop(Div,
           get_sexp_value_raw value mt ~at,
           get_sexp_value_raw divisor mt ~at);t=mt}
  | Sexp.List [Sexp.Atom "URem"; Sexp.Atom width; value; divisor] ->
    let guess = {precise=Unknown;
                 s=Sure Unsgn;
                 w=convert_str_to_width_confidence width} in
    let mt = ttype_of_guess guess in
    {v=Bop(Modulo,
           get_sexp_value_raw value mt ~at,
           get_sexp_value_raw divisor mt ~at);t=mt}
  | _ ->
    begin match get_slice_of_sexp exp t with
      | Some slice -> {v=slice;t}
      | None -> make_cmplx_val exp t
    end

let get_sexp_value exp ?(at=Beginning) t =
  simplify_tterm (get_sexp_value_raw exp ~at t)

let parse_condition cond =
  get_sexp_value (Sexp.of_string cond) Boolean

let parse_branches file =
  let branches_raw = Str.split (Str.regexp "\n+") file in
  let branches = List.map branches_raw ~f:(fun br ->
      let fields = Str.split (Str.regexp ", *") br in
      match fields with
      | [cond_raw; perf_raw] ->
        let cond_str = Str.split (Str.regexp " *\\*\\*AND\\*\\* *") cond_raw in
        let perf = int_of_string perf_raw in
        let cond = List.map cond_str ~f:(fun str -> (parse_condition str).v) in
        {cond;perf}
      | _ -> failwith (br ^ " is a malformed branch specification."))
  in
  branches