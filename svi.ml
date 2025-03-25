open Curses

module HList = struct
  let replace arr at with_new = 
    let rec inner counter arr = 
      match arr with
      | [] -> []
      | x :: xs ->
        if counter == at then with_new :: xs
        else x :: (inner (counter + 1) xs)
    in inner 0 arr

  let remove arr y =
    let rec inner counter arr = 
      match arr with
      | [] -> []
      | x :: xs -> 
        if counter == y then xs
        else x :: (inner (counter + 1) xs)
    in
    inner 0 arr

  let nth_opt l n = 
    try 
      List.nth_opt l n
    with
    | _ -> None
end

type mode =
  | Normal
  | Insert
[@@deriving show { with_path = false }]

type editor = {
  path : string;
  lines : string list [@opaque];
  cursor : int * int;
  mode : mode;
  exit : bool;
  displayed_line : int;
  rows : int
}
[@@deriving show { with_path = false }]

let init_curses () = 
  let window = initscr () in
  let err  = noecho () in
  let err = err || cbreak () in
  let err = err || keypad window true in
  (window, err)

let in_bounds_y ed y =
  let len = List.length ed.lines in
  if y > len - 1 then len - 1
  else if y <= 0 then 0
  else y

let in_bounds_x ed x y =
  let line = HList.nth_opt ed.lines y in
  match line with
  | Some line ->
    let len = String.length line in
    if len = 0 then 0
    else if x > len - 1 then len - 1
    else if x < 0 then 0
    else x
  | None -> 0

let check_for_page_limit ed y =
  let overflows_above = y < ed.displayed_line in
  let overflows_below = y > ed.displayed_line + ed.rows - 1 in
  if overflows_above then { ed with displayed_line = ed.displayed_line - 1 }
  else if overflows_below then { ed with displayed_line = ed.displayed_line + 1 }
  else ed

let move_to ed x y =
  let x = in_bounds_x ed x y in
  let y = in_bounds_y ed y in 
  let ed = check_for_page_limit ed y in
  { ed with cursor = (x, y) }

let save_editor ed =
  let oc = open_out ed.path in
  let content = String.concat "\n" ed.lines in
  output_string oc content;
  close_out oc

let get_gap ({ lines; cursor = (x, y); _ }) =
  let line = List.nth lines y in
  let first = String.sub line 0 x in
  let second = String.sub line x (String.length line - x) in
  (first, second)

let insert_character ({ lines; cursor = (_, y); _ } as ed) c =
    let (first, second) = get_gap ed in
    let new_line = first ^ (String.make 1 c) ^ second in
    { ed with lines = HList.replace lines y new_line }

let insert_newline ({ lines; cursor = (_, y); _ } as ed) =
  let (first, second) = get_gap ed in 
  let rec split_line index arr =
    match arr with
    | [] -> []
    | x :: xs -> 
      if index == y then first :: second :: xs
      else x :: (split_line (index + 1) xs)
  in 
  let lines = split_line 0 lines in
  { ed with lines = lines }

let merge_lines ({ cursor = (_, y); lines; _ } as ed) =
  let previous = List.nth lines (y - 1) in
  let current = List.nth lines y in
  let line = previous ^ current in
  let lines = HList.replace lines (y - 1) line in
  let lines = HList.remove lines y in
  { ed with lines = lines }

let remove_char ({ cursor = (_, y); lines; _ } as ed) =
  let (first, second) = get_gap ed in
  let first = String.sub first 0 (String.length first - 1) in
  let lines = HList.replace lines y (first^second) in
  { ed with lines = lines }

let handle_normal_action ed c = 
  let (x, y) = ed.cursor in
  match c with
  | 'q' -> { ed with exit = true }
  | 'w' -> save_editor ed; ed
  | 'j' -> move_to ed x (y + 1)
  | 'k' -> move_to ed x (y - 1)
  | 'l' -> move_to ed (x + 1) y
  | 'h' -> move_to ed (x - 1) y
  | 'i' -> { ed with mode = Insert }
  | _ -> ed

let handle_insert_action ed c =
  let (x, y) = ed.cursor in
  let d = int_of_char c in
  match d with
  | 27 -> { ed with mode = Normal }
  | 127 -> 
    (match (x, y) with
    | (0, 0) -> ed
    | (0, y) -> 
        let line = List.nth ed.lines (y - 1) in
        let len = String.length line in
        let ed = merge_lines ed in
        { ed with cursor = (len, y - 1) }
    | (_, _) -> 
        let ed = remove_char ed in
        move_to ed (x - 1) y
    )
  | 10 -> 
    let ed = insert_newline ed in
    move_to ed 0 (y + 1)
  | _ -> 
    let ed = insert_character ed c in
    move_to ed (x + 1) y

let handle_action ed c =
  match ed.mode with
  | Normal -> handle_normal_action ed c
  | Insert -> handle_insert_action ed c
  
let render ed = 
  let (x, y) = ed.cursor in
  clear ();
  (* let _ = addstr (show_editor ed) in *)
  for i = ed.displayed_line to (ed.displayed_line + ed.rows - 1) do
    let line = List.nth_opt ed.lines i in
    let _ = match line with
      | Some line -> addstr (line ^ "\n")
      | None -> addstr "~\n"
    in ()
  done;
  let _ = move (y - ed.displayed_line) x in
  let _ = refresh () in
  ()

let rec main_loop ed =
  let ch = char_of_int (getch ()) in
  let ed = handle_action ed ch in
  match ed.exit with
  | false -> 
    render ed;
    main_loop ed
  | true -> endwin ()

let loop ed =
  try 
    main_loop ed
  with
  | Failure msg -> endwin (); failwith msg
  | exn -> endwin (); raise exn

module File = struct
  let rec read_lines ic =
    try 
      let line = input_line ic in
      line :: read_lines ic
    with End_of_file -> []

  let read_file_lines filepath =
    let ic = open_in filepath in
    let lines = read_lines ic in
    close_in ic;
    lines
end

let read_file filepath = 
  let lines = File.read_file_lines filepath in
  { path = filepath; lines; cursor = (0, 0); mode = Normal; exit = false; displayed_line = 0; rows = 0 }

let () = 
  let filepath = Sys.argv.(1) in
  let (win, err) = init_curses () in
  let (rows, _) = getmaxyx win in
  let editor = read_file filepath in
  let editor = { editor with rows = rows } in
  let _ = render editor in
  match err with
  | false -> print_endline "Error while initializing curses."; endwin ()
  | true -> loop editor

  
