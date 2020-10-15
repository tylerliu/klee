codegen.byte: codegen.ml codegen_rules.ml ir.ml import.ml program_rules.ml
	corebuild -lib str codegen.byte -use-ocamlfind

pre_processing.byte: pre_processing.ml codegen_rules.ml ir.ml import.ml domain.ml
	corebuild -lib str pre_processing.byte -use-ocamlfind

neg_tree.byte: neg_tree.ml codegen_rules.ml ir.ml import.ml domain.ml
	corebuild -lib str neg_tree.byte -use-ocamlfind

clean:
	rm -f *.byte program_rules.ml